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
        const std::string& out_tensor_name = "",
        const std::string& left_tensor_name = "",
        const std::string& right_tensor_name = "",
        int input_3d_format = 0, int output_3d_format = 0, int separate_3d_buffer = 0)
        : HDMIConverterOp(holoscan::ArgList { holoscan::Arg { "allocator", allocator },
            holoscan::Arg { "cuda_device_ordinal", cuda_device_ordinal },
            holoscan::Arg { "out_tensor_name", out_tensor_name },
            holoscan::Arg { "left_tensor_name", left_tensor_name },
            holoscan::Arg { "right_tensor_name", right_tensor_name },
            holoscan::Arg { "input_3d_format", input_3d_format },
            holoscan::Arg { "output_3d_format", output_3d_format },
            holoscan::Arg { "separate_3d_buffer", separate_3d_buffer }})
    {
        name_ = name;
        fragment_ = fragment;
        spec_ = std::make_shared<holoscan::OperatorSpec>(fragment);
        setup(*spec_.get());
    }
};

PYBIND11_MODULE(_hdmi_converter, m)
{
#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif

    auto op = py::class_<HDMIConverterOp, PyHDMIConverterOp, holoscan::Operator, hololink::csi::CsiConverter,
        std::shared_ptr<HDMIConverterOp>>(m, "HDMIConverterOp")
                  .def(py::init<holoscan::Fragment*, const std::shared_ptr<holoscan::Allocator>&,
                           int, const std::string&, const std::string&,
                           const std::string&, const std::string&,
                           int, int, int>(),
                      "fragment"_a, "allocator"_a, "cuda_device_ordinal"_a = 0,
                      "name"_a = "hdmi_converter"s, "out_tensor_name"_a = ""s,
                      "left_tensor_name"_a = ""s, "right_tensor_name"_a = ""s,
                      "input_3d_format"_a = 0, "output_3d_format"_a = 0,
                      "separate_3d_buffer"_a = 0)
                  .def("setup", &HDMIConverterOp::setup, "spec"_a)
                  .def("configure", &HDMIConverterOp::configure,
                      "start_byte"_a, "bytes_per_line"_a, "pixel_width"_a, "pixel_height"_a,
                      "pixel_format"_a, "trailing_bytes"_a = 0)
                  .def("get_csi_length", &HDMIConverterOp::get_csi_length);

    // Bind Video3DFormat enum
    py::enum_<HDMIConverterOp::Video3DFormat>(op, "Video3DFormat", R"pbdoc(Video 3D Format enum)pbdoc")
        .value("INVALID", HDMIConverterOp::Video3DFormat::INVALID, R"pbdoc(Invalid 3D format)pbdoc")
        .value("FRAME_PACKING", HDMIConverterOp::Video3DFormat::FRAME_PACKING, R"pbdoc(
            Full-resolution left and right eye images stacked vertically.
            Commonly used in Blu-ray 3D, preserves full resolution.)pbdoc")
        .value("SIDE_BY_SIDE_HALF", HDMIConverterOp::Video3DFormat::SIDE_BY_SIDE_HALF, R"pbdoc(
            Left and right images compressed horizontally and placed side by side.
            Widely used in 3D TV broadcasts due to lower bandwidth requirements.)pbdoc")
        .value("TOP_AND_BOTTOM", HDMIConverterOp::Video3DFormat::TOP_AND_BOTTOM, R"pbdoc(
            Left and right images compressed vertically and stacked top and bottom.
            Common in streaming and broadcast scenarios with bandwidth constraints.)pbdoc")
        .value("LINE_BY_LINE", HDMIConverterOp::Video3DFormat::LINE_BY_LINE, R"pbdoc(
            Interleaved scanlines: odd lines for left eye, even lines for right eye.
            Supported by displays that handle line interleaving for 3D viewing.)pbdoc")
        .value("FIELD_ALTERNATIVE", HDMIConverterOp::Video3DFormat::FIELD_ALTERNATIVE, R"pbdoc(
            Interlaced fields: even field for one eye, odd field for the other.
            Used mainly in older interlaced (CRT) displays.)pbdoc")
        .value("TWO_D_PLUS_DEPTH", HDMIConverterOp::Video3DFormat::VIDEO_PLUS_DEPTH, R"pbdoc(
            Transmits a 2D image along with a depth map.
            Display reconstructs 3D effect using depth information.)pbdoc")
        .value("SIDE_BY_SIDE_FULL", HDMIConverterOp::Video3DFormat::SIDE_BY_SIDE_FULL, R"pbdoc(
            Full-resolution left and right eye images, each in a separate frame.
            Requires higher video bandwidth (e.g., HDMI 2.0 or DisplayPort).)pbdoc")
        .export_values();

} // PYBIND11_MODULE

} // namespace hololink::operators
