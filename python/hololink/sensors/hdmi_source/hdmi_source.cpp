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
#include <hololink/core/data_channel.hpp>

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

PYBIND11_MODULE(_hdmi_source, m)
{
#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif

    auto op = py::class_<HDMISource>(m, "HDMISource")
        .def(py::init<hololink::DataChannel*, uint32_t>(),
                "hololink_channel"_a, "i2c_controller_bus"_a = hololink::BL_I2C_BUS)
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
