/*
 * SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#ifndef SRC_OPERATORS_HDMI_CONVERTER_HDMI_CONVERTER
#define SRC_OPERATORS_HDMI_CONVERTER_HDMI_CONVERTER

#include <memory>

#include <holoscan/holoscan.hpp>
#include <holoscan/core/operator.hpp>
#include <holoscan/core/parameter.hpp>
#include <holoscan/utils/cuda_stream_handler.hpp>

#include <hololink/sensors/hdmi_source/hdmi_source.hpp>

#include <cuda.h>
#include <npp.h>

namespace hololink::common {

class CudaFunctionLauncher;

} // namespace hololink::common

namespace hololink::operators {

class HDMIConverterOp : public holoscan::Operator, public hololink::csi::CsiConverter {
public:
    HOLOSCAN_OPERATOR_FORWARD_ARGS(HDMIConverterOp);

    // 3D video format definitions (general, not limited to HDMI)
    enum Video3DFormat {
        INVALID = 0,            // Invalid or uninitialized format

        FRAME_PACKING = 1,      // Full-resolution left and right eye images stacked vertically
                                // Commonly used in Blu-ray 3D, preserves full resolution

        SIDE_BY_SIDE_HALF = 2,  // Left and right images compressed horizontally and placed side by side
                                // Widely used in 3D TV broadcasts due to lower bandwidth requirements

        TOP_AND_BOTTOM = 3,     // Left and right images compressed vertically and stacked top and bottom
                                // Common in streaming and broadcast scenarios with bandwidth constraints

        LINE_BY_LINE = 4,       // Interleaved scanlines: odd lines for left eye, even lines for right eye
                                // Supported by displays that handle line interleaving for 3D viewing

        FIELD_ALTERNATIVE = 5,  // Interlaced fields: even field for one eye, odd field for the other
                                // Used mainly in older interlaced (CRT) displays

        VIDEO_PLUS_DEPTH = 6,      // Transmits a 2D image along with a depth map
                                // Display reconstructs 3D effect using depth information

        SIDE_BY_SIDE_FULL = 7   // Full-resolution left and right eye images, each in a separate frame
                                // Requires higher video bandwidth (e.g., HDMI 2.0 or DisplayPort)
    };

    void start() override;
    void stop() override;
    void setup(holoscan::OperatorSpec& spec) override;
    void compute(holoscan::InputContext&, holoscan::OutputContext& op_output,
        holoscan::ExecutionContext&) override;

    uint32_t receiver_start_byte() override;
    uint32_t received_line_bytes(uint32_t line_bytes) override;
    uint32_t transmitted_line_bytes(hololink::csi::PixelFormat pixel_format, uint32_t pixel_width) override;
    void configure(uint32_t start_byte, uint32_t bytes_per_line, uint32_t pixel_width, uint32_t pixel_height, hololink::csi::PixelFormat pixel_format, uint32_t trailing_bytes) override;

    size_t get_csi_length();

private:
    holoscan::Parameter<std::shared_ptr<holoscan::Allocator>> allocator_;
    holoscan::Parameter<int> cuda_device_ordinal_;
    holoscan::Parameter<std::string> out_tensor_name_;
    holoscan::Parameter<std::string> left_tensor_name_;
    holoscan::Parameter<std::string> right_tensor_name_;
    holoscan::Parameter<int> input_3d_format_;
    holoscan::Parameter<int> output_3d_format_;
    holoscan::Parameter<int> separate_3d_buffer_;

    CUcontext cuda_context_ = nullptr;
    CUdevice cuda_device_ = 0;
    bool is_integrated_ = false;
    bool host_memory_warning_ = false;

    holoscan::CudaStreamHandler cuda_stream_handler_;

    std::shared_ptr<hololink::common::CudaFunctionLauncher> cuda_function_launcher_;
    std::unique_ptr<nvidia::gxf::MemoryBuffer> left_eye_buffer_;
    std::unique_ptr<nvidia::gxf::MemoryBuffer> right_eye_buffer_;
    std::unique_ptr<nvidia::gxf::MemoryBuffer> left_eye_resize_buffer_;
    std::unique_ptr<nvidia::gxf::MemoryBuffer> right_eye_resize_buffer_;
    std::unique_ptr<nvidia::gxf::MemoryBuffer> device_scratch_buffer_;

    uint32_t pixel_width_ = 0;
    uint32_t pixel_height_ = 0;
    hololink::csi::PixelFormat pixel_format_ = hololink::csi::PixelFormat::RAW_8;
    uint32_t start_byte_ = 0;
    uint32_t bytes_per_line_ = 0;
    size_t csi_length_ = 0;
    bool configured_ = false;
};

} // namespace hololink::operators

#endif /* SRC_OPERATORS_HDMI_CONVERTER_HDMI_CONVERTER */
