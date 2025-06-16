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

#include "hdmi_source.hpp"


#include <hololink/core/hololink.hpp>
#include <hololink/core/logging_internal.hpp>
#include <hololink/core/deserializer.hpp>
#include <hololink/core/serializer.hpp>
#include <hololink/core/data_channel.hpp>

#include <thread>
#include <chrono>
#include <vector>

namespace hololink::sensors {

HDMISource::HDMISource(hololink::DataChannel* hololink_channel, uint32_t i2c_controller_bus)
{
    hololink_channel_ = hololink_channel;
    hololink_ = hololink_channel->hololink();
    i2c_ = hololink_->get_i2c(i2c_controller_bus);
    width_ = 1920;
    height_ = 1080;
    pixel_format_ = hololink::csi::PixelFormat::YUYV_8;

#if 1
    set_register(0x03, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    set_register(0x03, 1);

#if 0
    for (int i = 0; i < 16; i++) {
        int value = get_register(i);
        HSB_LOG_INFO(fmt::format("{:02x} {:02x}", i, value));
    }
#endif
    int VLow = get_register(0x08);
    int VHigh = get_register(0x09);
    int HLow = get_register(0x0A);
    int HHigh = get_register(0x0B);
    video_frame_rate_ = get_register(0x0C);
    video_width_ = ((HHigh & 0xff) << 8) + (HLow & 0xff);
    video_height_ = ((VHigh & 0xff) << 8) + (VLow & 0xff);

    HSB_LOG_INFO(fmt::format("width {} height {} frame rate {}", video_width_, video_height_, video_frame_rate_));
    if (video_width_ != 0 && video_height_ != 0) {
        width_ = video_width_;
        height_ = video_height_;
    }
    set_register(0x03, 0);
#endif
}

void HDMISource::setup_clock() {
    set_register(0x03, 0);
}

void HDMISource::start()
{
    HSB_LOG_INFO("HDMI Source start");
    running_ = true;
    set_register(0x03, 0);
    //std::this_thread::sleep_for(std::chrono::seconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    set_register(0x03, 1);

#if 0
    for (int i = 0; i < 100; i++) {
        int value = get_register(i);
        HSB_LOG_INFO(fmt::format("{} {}", i, value));
    }
#endif
}

void HDMISource::stop()
{
    HSB_LOG_INFO("HDMI Source stop");
#if 0
    for (int i = 0; i < 100; i++) {
        int value = get_register(i);
        HSB_LOG_INFO(fmt::format("{} {}", i, value));
    }
#endif
    set_register(0x03, 0);
    running_ = false;
}

int HDMISource::get_version() {
    return VERSION;
}

int32_t HDMISource::get_register(uint32_t register_addr) {
    std::vector<uint8_t> write_bytes(1);
    core::Serializer serializer(write_bytes.data(), write_bytes.size());
    serializer.append_uint8(register_addr);
    uint32_t read_byte_count = 1;
    std::vector<uint8_t> reply = i2c_->i2c_transaction(CAM_I2C_ADDRESS, write_bytes, read_byte_count);
    auto deserializer = std::make_shared<core::Deserializer>(reply.data(), reply.size());
    uint8_t value;
    deserializer->next_uint8(value);
    //HSB_LOG_INFO(fmt::format("{} {}", register_addr, value));
    return value;
}

void HDMISource::set_register(uint32_t register_addr, uint32_t value, std::shared_ptr<Timeout> timeout) {
    std::vector<uint8_t> write_bytes(2);
    core::Serializer serializer(write_bytes.data(), write_bytes.size());
    serializer.append_uint8(register_addr);
    uint8_t value_8 = value;
    serializer.append_uint8(value_8);
    uint32_t read_byte_count = 0;
    i2c_->i2c_transaction(CAM_I2C_ADDRESS, write_bytes, read_byte_count);
}

void HDMISource::configure_converter(std::shared_ptr<hololink::csi::CsiConverter> converter) {
    // Get starting byte position
    uint32_t start_byte = converter->receiver_start_byte();

    // Calculate transmitted and received line bytes
    uint32_t transmitted_line_bytes = converter->transmitted_line_bytes(pixel_format_, width_);
    uint32_t received_line_bytes = converter->received_line_bytes(transmitted_line_bytes);

    start_byte += 0;

    // Configure the converter
    converter->configure(
        start_byte,
        received_line_bytes,
        width_,
        height_,
        pixel_format_);
}

}
