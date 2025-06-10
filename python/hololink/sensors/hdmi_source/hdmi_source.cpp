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

#include <hololink/sensors/hdmi_source/hdmi_source.hpp>
#include <hololink/data_channel.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h> // for unordered_map -> dict, etc.

#include <cstdint>
#include <memory>
#include <string>

using std::string_literals::operator""s;
using pybind11::literals::operator""_a;

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace py = pybind11;

namespace hololink::sensors {

// Trampoline class
class PyHDMIConverterBase : public HDMIConverterBase {
public:
    using HDMIConverterBase::PixelFormat;

    void configure(uint32_t width, uint32_t height, PixelFormat pixel_format,
                   uint32_t frame_start_size, uint32_t frame_end_size,
                   uint32_t line_start_size, uint32_t line_end_size,
                   uint32_t margin_left = 0, uint32_t margin_top = 0,
                   uint32_t margin_right = 0, uint32_t margin_bottom = 0) override {
        PYBIND11_OVERRIDE_PURE(
            void,
            HDMIConverterBase,
            configure,
            width, height, pixel_format,
            frame_start_size, frame_end_size,
            line_start_size, line_end_size,
            margin_left, margin_top,
            margin_right, margin_bottom
        );
    }
};

PYBIND11_MODULE(_hdmi_source, m)
{
#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif

    // HDMIConverterBase class
    auto base = py::class_<HDMIConverterBase, PyHDMIConverterBase, std::shared_ptr<HDMIConverterBase>>(m, "HDMIConverterBase")
        .def(py::init<>())
        .def("configure", &HDMIConverterBase::configure);

    // enum nested under HDMIConverterBase
    py::enum_<HDMIConverterBase::PixelFormat>(base, "PixelFormat")
        .value("INVALID", HDMIConverterBase::PixelFormat::INVALID)
        .value("RGB_8", HDMIConverterBase::PixelFormat::RGB_8)
        .value("YUYV_8", HDMIConverterBase::PixelFormat::YUYV_8)
        .value("NV12_8", HDMIConverterBase::PixelFormat::NV12_8)
        .export_values();

    auto op = py::class_<HDMISource>(m, "HDMISource")
        .def(py::init<hololink::DataChannel*, uint32_t>(),
                "hololink_channel"_a, "i2c_controller_address"_a = hololink::BL_I2C_CTRL)
        .def_property("_width", &HDMISource::width, &HDMISource::set_width)
        .def_property("_height", &HDMISource::height, &HDMISource::set_height)
        .def("setup_clock", &HDMISource::setup_clock)
        .def("start", &HDMISource::start)
        .def("stop", &HDMISource::stop)
        .def("get_version", &HDMISource::get_version)
        .def("get_register", &HDMISource::get_register, "register_addr"_a)
        .def("set_register", &HDMISource::set_register,
             "register_addr"_a, "value"_a, "timeout"_a = nullptr)
        .def("configure_converter", &HDMISource::configure_converter,
             "converter"_a);

} // PYBIND11_MODULE

} // namespace hololink::operators
