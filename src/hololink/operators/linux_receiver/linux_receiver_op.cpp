/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "linux_receiver_op.hpp"

#include <chrono>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sched.h>
#include <unistd.h>

#include <netinet/in.h>

#include <hololink/common/cuda_helper.hpp>
#include <hololink/core/data_channel.hpp>
#include <hololink/core/logging_internal.hpp>
#include <hololink/core/networking.hpp>

#include "linux_receiver.hpp"

namespace hololink::operators {

void LinuxReceiverOp::initialize()
{
    // Set default identity function if rename_metadata is not set
    if (!rename_metadata_) {
        rename_metadata_ = [](const std::string& name) { return name; };
    }

    // Cache the metadata key names using the rename callback
    const auto& rename_fn = rename_metadata_;

    frame_packets_received_metadata_ = rename_fn("frame_packets_received");
    frame_number_metadata_ = rename_fn("frame_number");
    received_frame_number_metadata_ = rename_fn("received_frame_number");
    frame_bytes_received_metadata_ = rename_fn("frame_bytes_received");
    received_s_metadata_ = rename_fn("received_s");
    received_ns_metadata_ = rename_fn("received_ns");
    timestamp_s_metadata_ = rename_fn("timestamp_s");
    timestamp_ns_metadata_ = rename_fn("timestamp_ns");
    metadata_s_metadata_ = rename_fn("metadata_s");
    metadata_ns_metadata_ = rename_fn("metadata_ns");
    packets_dropped_metadata_ = rename_fn("packets_dropped");
    crc_metadata_ = rename_fn("crc");
    psn_metadata_ = rename_fn("psn");
    bytes_written_metadata_ = rename_fn("bytes_written");

    const char* env = std::getenv("HOLOLINK_AFFINITY");
    if (env == nullptr) {
        // 預設 core = 2
        receiver_affinity_ = 2;
        HSB_LOG_INFO("Affinity Default to CPU {}", receiver_affinity_);
        use_affinity_ = true;
    } else if (std::strlen(env) == 0) {
        // 空字串，代表不綁定
        use_affinity_ = false;
    } else {
        // 環境變數有設數字
        receiver_affinity_ = std::stoi(env);
        HSB_LOG_INFO("Affinity env to CPU {}", receiver_affinity_);
        use_affinity_ = true;
    }

    // Call base class initialize
    BaseReceiverOp::initialize();
}

void LinuxReceiverOp::set_rename_metadata(std::function<std::string(const std::string&)> rename_fn)
{
    rename_metadata_ = rename_fn;
}

void LinuxReceiverOp::start_receiver()
{
    check_buffer_size(frame_size_.get());
    size_t metadata_address = hololink::core::round_up(frame_size_.get(), hololink::core::PAGE_SIZE);
    // received_frame_size wants to be page aligned; prove that METADATA_SIZE doesn't upset that.
    // Prove that PAGE_SIZE is a power of two
    static_assert((hololink::core::PAGE_SIZE & (hololink::core::PAGE_SIZE - 1)) == 0);
    // Prove that METADATA_SIZE is an even multiple of PAGE_SIZE
    static_assert((hololink::METADATA_SIZE & (hololink::core::PAGE_SIZE - 1)) == 0);
    size_t received_frame_size = metadata_address + hololink::METADATA_SIZE;
    size_t buffer_size = hololink::core::round_up(received_frame_size * PAGES, getpagesize());
    frame_memory_.reset(new ReceiverMemoryDescriptor(frame_context_, buffer_size));
    HSB_LOG_INFO("frame_size={:#x} frame={:#x} buffer_size={:#x}", frame_size_.get(), frame_memory_->get(), buffer_size);
    HSB_LOG_INFO("frame_size={} frame={} buffer_size={}", frame_size_.get(), frame_memory_->get(), buffer_size);

    receiver_.reset(new LinuxReceiver(
        frame_memory_->get(),
        frame_size_.get(),
        data_socket_.get(),
        received_address_offset()));

    receiver_->set_frame_ready([this](const LinuxReceiver&) {
        this->frame_ready();
    });
    hololink_channel_->authenticate(receiver_->get_qp_number(), receiver_->get_rkey());

    receiver_thread_.reset(new std::thread(&hololink::operators::LinuxReceiverOp::run, this));
    const int error = pthread_setname_np(receiver_thread_->native_handle(), name().c_str());
    if (error != 0) {
        throw std::runtime_error("Failed to set thread name");
    }

    auto [local_ip, local_port] = local_ip_and_port();
    HSB_LOG_INFO("local_ip={} local_port={}", local_ip, local_port);

    uint64_t distal_memory_address_start = 0;  // See received_address_offset()
    hololink_channel_->configure_roce(distal_memory_address_start, frame_size_, received_frame_size, PAGES, local_port);
}

void LinuxReceiverOp::run()
{
    CudaCheck(cuCtxSetCurrent(frame_context_));

    if (use_affinity_) {
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(receiver_affinity_, &mask);

        pid_t pid = 0; // 0 = 當前執行緒/程序
        if (sched_setaffinity(pid, sizeof(mask), &mask) != 0) {
            HSB_LOG_ERROR("Affinity sched_setaffinity");
        } else {
            HSB_LOG_INFO("Affinity Pinned to CPU {}", receiver_affinity_);
        }
    } else {
        HSB_LOG_INFO("Affinity No affinity configured");
    }
    receiver_->run();
}

void LinuxReceiverOp::check_buffer_size(size_t data_memory_size) {
    // Get current receiver buffer size
    int receiver_buffer_size = 0;
    socklen_t optlen = sizeof(receiver_buffer_size);
    if (getsockopt(data_socket_.get(), SOL_SOCKET, SO_RCVBUF, &receiver_buffer_size, &optlen) < 0) {
        std::cerr << "getsockopt failed: " << strerror(errno) << "\n";
        close(data_socket_.get());
        return;
    }

    if (receiver_buffer_size < data_memory_size) {
        // Round up to a 64 KB boundary
        int boundary = 0x10000 - 1;
        int request_size = (data_memory_size + boundary) & ~boundary;

        if (setsockopt(data_socket_.get(), SOL_SOCKET, SO_RCVBUF, &request_size, sizeof(request_size)) < 0) {
            HSB_LOG_ERROR("setsockopt failed: {}", strerror(errno));
        }

        // Get the actual buffer size set by the kernel
        if (getsockopt(data_socket_.get(), SOL_SOCKET, SO_RCVBUF, &receiver_buffer_size, &optlen) < 0) {
            HSB_LOG_ERROR("setsockopt failed: {}", strerror(errno));
        }

        HSB_LOG_INFO("Receiver buffer size={} request_size= |{}", receiver_buffer_size, request_size);

        if (receiver_buffer_size < data_memory_size) {
            HSB_LOG_WARN("Kernel receiver buffer size is too small; performance may be unreliable.\n");
            HSB_LOG_WARN("Resolve with: echo {} | sudo tee /proc/sys/net/core/rmem_max\n", request_size);
        }
    }
}

uint64_t LinuxReceiverOp::received_address_offset() {
    return static_cast<uint64_t>(frame_memory_->get());
}

void LinuxReceiverOp::stop_receiver()
{
    hololink_channel_->unconfigure();
    data_socket_.reset();
    receiver_->close();
    receiver_thread_->join();
    receiver_thread_.reset();
    frame_memory_.reset();
}

std::tuple<CUdeviceptr, std::shared_ptr<hololink::Metadata>> LinuxReceiverOp::get_next_frame(double timeout_ms)
{
    //HSB_LOG_INFO("get_next_frame\n");

    LinuxReceiverMetadata receiver_metadata;
    if (!receiver_->get_next_frame(timeout_ms, receiver_metadata)) {
        return {};
    }

    CUdeviceptr frame_memory = frame_memory_->get();

    auto metadata = std::make_shared<Metadata>();
    (*metadata)[frame_packets_received_metadata_] = int64_t(receiver_metadata.frame_packets_received);
    (*metadata)[frame_bytes_received_metadata_] = int64_t(receiver_metadata.frame_bytes_received);
    (*metadata)[frame_number_metadata_] = int64_t(receiver_metadata.frame_number);
    (*metadata)[received_frame_number_metadata_] = int64_t(receiver_metadata.received_frame_number);
    (*metadata)[received_s_metadata_] = int64_t(receiver_metadata.received_s);
    (*metadata)[received_ns_metadata_] = int64_t(receiver_metadata.received_ns);
    (*metadata)[timestamp_s_metadata_] = int64_t(receiver_metadata.frame_metadata.timestamp_s);
    (*metadata)[timestamp_ns_metadata_] = int64_t(receiver_metadata.frame_metadata.timestamp_ns);
    (*metadata)[metadata_s_metadata_] = int64_t(receiver_metadata.frame_metadata.metadata_s);
    (*metadata)[metadata_ns_metadata_] = int64_t(receiver_metadata.frame_metadata.metadata_ns);
    (*metadata)[packets_dropped_metadata_] = int64_t(receiver_metadata.packets_dropped);
    (*metadata)[crc_metadata_] = int64_t(receiver_metadata.frame_metadata.crc);
    (*metadata)[psn_metadata_] = int64_t(receiver_metadata.frame_metadata.psn);
    (*metadata)[bytes_written_metadata_] = int64_t(receiver_metadata.frame_metadata.bytes_written);

    return { frame_memory, metadata };
}

} // namespace hololink::operators
