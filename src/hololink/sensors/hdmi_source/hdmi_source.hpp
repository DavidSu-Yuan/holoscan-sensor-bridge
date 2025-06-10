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

#ifndef SRC_SENSORS_HDMI_SOURCE_HDMI_SOURCE
#define SRC_SENSORS_HDMI_SOURCE_HDMI_SOURCE

#include <memory>

#include <hololink/hololink.hpp>
#include "hololink/timeout.hpp"

namespace hololink::sensors {

class HDMIConverterBase {
    public:
        enum class PixelFormat {
            INVALID = -1,
            RGB_8 = 0,
            YUYV_8 = 1,
            NV12_8 = 2,
        };

        virtual void configure(uint32_t width, uint32_t height, PixelFormat pixel_format,
            uint32_t frame_start_size, uint32_t frame_end_size, uint32_t line_start_size,
            uint32_t line_end_size, uint32_t margin_left = 0, uint32_t margin_top = 0,
            uint32_t margin_right = 0, uint32_t margin_bottom = 0) = 0;
};

class HDMISource {
public:
    constexpr static int VERSION = 1;
    constexpr static uint32_t CAM_I2C_ADDRESS = 0b00110010;
    HDMISource(hololink::DataChannel* hololink_channel, uint32_t i2c_controller_address = hololink::BL_I2C_CTRL);
    void setup_clock();
    void start();
    void stop();
    int get_version();
    int get_register(uint32_t register_addr);
    void set_register(uint32_t register_addr, uint32_t value,
            std::shared_ptr<Timeout> timeout = std::shared_ptr<Timeout>());
    void configure_converter(std::shared_ptr<HDMIConverterBase> converter);
    uint32_t width() const { return width_; }
    void set_width(uint32_t w) { width_ = w; }
    uint32_t height() const { return height_; }
    void set_height(uint32_t w) { height_ = w; }

private:
    uint32_t width_ = 1920;
    uint32_t height_ = 1080;
    HDMIConverterBase::PixelFormat pixel_format_ = HDMIConverterBase::PixelFormat::YUYV_8;
    bool running_ = false;

    DataChannel* hololink_channel_ = nullptr;
    std::shared_ptr<hololink::Hololink> hololink_ = nullptr;
    std::shared_ptr<hololink::Hololink::I2c> i2c_ = nullptr;
};

} // namespace hololink::sensors

#endif /* SRC_SENSORS_HDMI_SOURCE_HDMI_SOURCE */
