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

#include <hololink/core/csi_controller.hpp>
#include <hololink/core/csi_formats.hpp>
#include <hololink/core/hololink.hpp>
#include <hololink/core/timeout.hpp>

namespace hololink::sensors {

class HDMISource {
public:
    constexpr static uint32_t VERSION = 1;
    constexpr static uint32_t CAM_I2C_ADDRESS = 0b00110010;
    HDMISource(hololink::DataChannel* hololink_channel, uint32_t i2c_controller_bus = hololink::BL_I2C_BUS);
    void setup_clock();
    void start();
    void stop();
    int get_version();
    int get_register(uint32_t register_addr);
    void set_register(uint32_t register_addr, uint32_t value,
            std::shared_ptr<Timeout> timeout = std::shared_ptr<Timeout>());
    void configure_converter(std::shared_ptr<hololink::csi::CsiConverter> converter);
    uint32_t width() const { return width_; }
    void set_width(uint32_t w) { width_ = w; }
    uint32_t height() const { return height_; }
    void set_height(uint32_t w) { height_ = w; }

private:
    uint32_t width_ = 1920;
    uint32_t height_ = 1080;
    uint32_t video_width_ = 1920;
    uint32_t video_height_ = 1080;
    uint32_t video_frame_rate_ = 60;
    hololink::csi::PixelFormat pixel_format_ = hololink::csi::PixelFormat::YUYV_8;
    bool running_ = false;

    DataChannel* hololink_channel_ = nullptr;
    std::shared_ptr<hololink::Hololink> hololink_ = nullptr;
    std::shared_ptr<hololink::Hololink::I2c> i2c_ = nullptr;
};

} // namespace hololink::sensors

#endif /* SRC_SENSORS_HDMI_SOURCE_HDMI_SOURCE */
