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

#include <hololink/operators/hdmi_converter/hdmi_converter.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h> // for unordered_map -> dict, etc.

#include <cstdint>
#include <memory>
#include <string>

#include <holoscan/core/fragment.hpp>
#include <holoscan/core/operator.hpp>
#include <holoscan/core/operator_spec.hpp>
#include <holoscan/core/resources/gxf/allocator.hpp>

using std::string_literals::operator""s;
using pybind11::literals::operator""_a;

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace py = pybind11;

namespace hololink::operators {

/* Trampoline classes for handling Python kwargs
 *
 * These add a constructor that takes a Fragment for which to initialize the operator.
 * The explicit parameter list and default arguments take care of providing a Pythonic
 * kwarg-based interface with appropriate default values matching the operator's
 * default parameters in the C++ API `setup` method.
 *
 * The sequence of events in this constructor is based on Fragment::make_operator<OperatorT>
 */
class PyHDMIConverterOp : public HDMIConverterOp {
public:
    /* Inherit the constructors */
    using HDMIConverterOp::HDMIConverterOp;

    // Define a constructor that fully initializes the object.
    PyHDMIConverterOp(holoscan::Fragment* fragment,
        const std::shared_ptr<holoscan::Allocator>& allocator, int cuda_device_ordinal,
        const std::string& name = "hdmi_converter",
        const std::string& out_tensor_name = "")
        : HDMIConverterOp(holoscan::ArgList { holoscan::Arg { "allocator", allocator },
            holoscan::Arg { "cuda_device_ordinal", cuda_device_ordinal },
            holoscan::Arg { "out_tensor_name", out_tensor_name } })
    {
        name_ = name;
        fragment_ = fragment;
        spec_ = std::make_shared<holoscan::OperatorSpec>(fragment);
        setup(*spec_.get());
    }

    void configure(uint32_t width, uint32_t height, PixelFormat pixel_format,
                   uint32_t frame_start_size, uint32_t frame_end_size,
                   uint32_t line_start_size, uint32_t line_end_size,
                   uint32_t margin_left, uint32_t margin_top,
                   uint32_t margin_right, uint32_t margin_bottom) override {
        PYBIND11_OVERRIDE(
            void,              // Return type
            HDMIConverterOp,   // Parent class (must match base)
            configure,         // Name of function in C++
            width, height, pixel_format,
            frame_start_size, frame_end_size,
            line_start_size, line_end_size,
            margin_left, margin_top,
            margin_right, margin_bottom);
    }
};

PYBIND11_MODULE(_hdmi_converter, m)
{
#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif

    auto op = py::class_<HDMIConverterOp, PyHDMIConverterOp, holoscan::Operator, hololink::sensors::HDMIConverterBase,
        std::shared_ptr<HDMIConverterOp>>(m, "HDMIConverterOp")
                  .def(py::init<holoscan::Fragment*, const std::shared_ptr<holoscan::Allocator>&,
                           int, const std::string&, const std::string&>(),
                      "fragment"_a, "allocator"_a, "cuda_device_ordinal"_a = 0,
                      "name"_a = "hdmi_converter"s, "out_tensor_name"_a = ""s)
                  .def("setup", &HDMIConverterOp::setup, "spec"_a)
                  .def("configure", &HDMIConverterOp::configure, "width"_a, "height"_a,
                      "pixel_format"_a, "frame_start_size"_a, "frame_end_size"_a,
                      "line_start_size"_a, "line_end_size"_a, "margin_left"_a = 0,
                      "margin_top"_a = 0, "margin_right"_a = 0, "margin_bottom"_a = 0)
                  .def("get_frame_length", &HDMIConverterOp::get_frame_length);

#if 0
    py::enum_<HDMIConverterOp::PixelFormat>(op, "PixelFormat")
        .value("RGB_8", HDMIConverterOp::PixelFormat::RGB_8, R"pbdoc(RGB 8-bit)pbdoc")
        .value("YUYV_8", HDMIConverterOp::PixelFormat::YUYV_8, R"pbdoc(YUYV 8-bit)pbdoc")
        .value("NV12_8", HDMIConverterOp::PixelFormat::NV12_8, R"pbdoc(NV12 8-bit)pbdoc")
        .export_values();
#endif

} // PYBIND11_MODULE

} // namespace hololink::operators
