/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <hololink/core/logging_internal.hpp>

static int indent_level = 0;

static std::string indent()
{
    std::stringstream ss;
    for (auto i = indent_level; i > 0; --i) {
        ss << "  ";
    }
    return ss.str();
}

static std::string start_indent_block()
{
    std::stringstream ss;
    ss << indent() << "{\n";
    indent_level++;
    return ss.str();
}

static std::string end_indent_block(bool trailing_comma = false)
{
    indent_level--;
    std::stringstream ss;
    ss << indent() << "}" << (trailing_comma ? "," : "") << "\n";
    return ss.str();
}

static std::string ip_to_str(uint32_t ip)
{
    if (ip == 0) {
        return "N/A";
    }
    std::stringstream ss;
    ss << (ip & 0xFF) << ".";
    ss << ((ip >> 8) & 0xFF) << ".";
    ss << ((ip >> 16) & 0xFF) << ".";
    ss << ((ip >> 24) & 0xFF);
    return ss.str();
}

template <>
struct fmt::formatter<nvsipl::MacAddress> : fmt::formatter<fmt::string_view> {
    auto format(const nvsipl::MacAddress& mac, fmt::format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "{:x}:{:x}:{:x}:{:x}:{:x}:{:x}",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
};

template <>
struct fmt::formatter<nvsipl::CoESensor> : fmt::formatter<fmt::string_view> {
    auto format(const nvsipl::CoESensor& sensor, fmt::format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(),
            "{}macAddress = {},\n"
            "{}ipAddress = {}\n",
            indent(), sensor.macAddress,
            indent(), ip_to_str(sensor.ipAddress));
    }
};

template <>
struct fmt::formatter<nvsipl::CoECamera> : fmt::formatter<fmt::string_view> {
    auto format(const nvsipl::CoECamera& camera, fmt::format_context& ctx) const -> decltype(ctx.out())
    {
        size_t num_cameras = camera.isStereo ? 2 : 1;
        std::stringstream ss;
        ss << fmt::format(
            "{}hsbId = {},\n"
            "{}isStereo = {},\n"
            "{}hsbSensorIndex = {},\n"
            "{}sensors[{}] =\n",
            indent(), camera.hsbId,
            indent(), camera.isStereo,
            indent(), camera.hsbSensorIndex,
            indent(), num_cameras);
        ss << start_indent_block();
        if (num_cameras > 1 || camera.hsbSensorIndex == 0) {
            ss << start_indent_block();
            ss << fmt::format("{}", camera.sensors[0]);
            ss << end_indent_block();
        }
        if (num_cameras > 1 || camera.hsbSensorIndex == 1) {
            ss << start_indent_block();
            ss << fmt::format("{}", camera.sensors[1]);
            ss << end_indent_block();
        }
        ss << end_indent_block();
        return fmt::format_to(ctx.out(), "{}", ss.str());
    }
};

template <>
struct fmt::formatter<nvsipl::CameraType> : fmt::formatter<fmt::string_view> {
    auto format(const nvsipl::CameraType& camera_type, fmt::format_context& ctx) const -> decltype(ctx.out())
    {
        if (std::holds_alternative<nvsipl::CoECamera>(camera_type)) {
            const auto& coe_camera = std::get<nvsipl::CoECamera>(camera_type);
            return fmt::format_to(ctx.out(), "{}", coe_camera);
        } else {
            return fmt::format_to(ctx.out(), "GsmlCamera");
        }
    }
};

template <>
struct fmt::formatter<nvsipl::CoETransSettings> : fmt::formatter<fmt::string_view> {
    auto format(const nvsipl::CoETransSettings& transport, fmt::format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(),
            "{}interfaceName = {},\n"
            "{}name = {},\n"
            "{}ipAddress = {},\n"
            "{}hsbId = {},\n"
            "{}vlanEnable = {},\n"
            "{}syncSensors = {}\n",
            indent(), transport.interfaceName,
            indent(), transport.name,
            indent(), ip_to_str(transport.ipAddress),
            indent(), transport.hsbId,
            indent(), transport.vlanEnable,
            indent(), transport.syncSensors);
    }
};

template <>
struct fmt::formatter<nvsipl::SensorInfo> : fmt::formatter<fmt::string_view> {
    auto format(const nvsipl::SensorInfo& sensor, fmt::format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(),
            "{}resolution = {}x{} @ {} fps,\n"
            "{}cfa = {},\n"
            "{}embeddedTopLines = {},\n"
            "{}embeddedBottomLines = {},\n"
            "{}inputFormat = {},\n"
            "{}i2cAddress = {}\n",
            indent(), sensor.vcInfo.resolution.width, sensor.vcInfo.resolution.height, sensor.vcInfo.fps,
            indent(), sensor.vcInfo.cfa,
            indent(), sensor.vcInfo.embeddedTopLines,
            indent(), sensor.vcInfo.embeddedBottomLines,
            indent(), static_cast<int>(sensor.vcInfo.inputFormat),
            indent(), sensor.i2cAddress);
    }
};

template <>
struct fmt::formatter<nvsipl::CameraConfig> : fmt::formatter<fmt::string_view> {
    auto format(const nvsipl::CameraConfig& camera, fmt::format_context& ctx) const -> decltype(ctx.out())
    {
        std::stringstream camera_type;
        camera_type << start_indent_block();
        camera_type << fmt::format("{}", camera.cameratype);
        camera_type << end_indent_block(true);
        std::stringstream sensor_info;
        sensor_info << start_indent_block();
        sensor_info << fmt::format("{}", camera.sensorInfo);
        sensor_info << end_indent_block();
        return fmt::format_to(ctx.out(),
            "{}name = {},\n"
            "{}platform = {},\n"
            "{}platformConfig = {},\n"
            "{}description = \"{}\",\n"
            "{}cameratype =\n{}"
            "{}sensorInfo =\n{}",
            indent(), camera.name,
            indent(), camera.platform,
            indent(), camera.platformConfig,
            indent(), camera.description,
            indent(), camera_type.str(),
            indent(), sensor_info.str());
    }
};

template <>
struct fmt::formatter<nvsipl::TransportConfig> : fmt::formatter<fmt::string_view> {
    auto format(const nvsipl::TransportConfig& transport, fmt::format_context& ctx) const -> decltype(ctx.out())
    {
        if (std::holds_alternative<nvsipl::CoETransSettings>(transport)) {
            const auto& coe_transport = std::get<nvsipl::CoETransSettings>(transport);
            return fmt::format_to(ctx.out(), "{}", coe_transport);
        } else {
            return fmt::format_to(ctx.out(), "GsmlTransportSettings");
        }
    }
};

template <>
struct fmt::formatter<nvsipl::CameraSystemConfig> : fmt::formatter<fmt::string_view> {
    auto format(const nvsipl::CameraSystemConfig& config, fmt::format_context& ctx) const -> decltype(ctx.out())
    {
        std::stringstream ss;
        ss << start_indent_block();

        // Cameras.
        ss << indent() << "cameras[" << config.cameras.size() << "] =\n";
        ss << start_indent_block();
        for (uint32_t i = 0; i < config.cameras.size(); ++i) {
            const auto& camera = config.cameras[i];
            ss << start_indent_block();
            ss << fmt::format("{}", camera);
            ss << end_indent_block((i + 1) < config.cameras.size());
        }
        ss << end_indent_block(true);

        // Transports.
        ss << indent() << "transports[" << config.transports.size() << "] =\n";
        ss << start_indent_block();
        for (uint32_t i = 0; i < config.transports.size(); ++i) {
            const auto& transport = config.transports[i];
            ss << start_indent_block();
            ss << fmt::format("{}", transport);
            ss << end_indent_block((i + 1) < config.transports.size());
        }
        ss << end_indent_block();

        ss << end_indent_block();
        return fmt::format_to(ctx.out(), "{}", ss.str());
    }
};

template <>
struct fmt::formatter<NvSciBufAttrValColorFmt> : fmt::formatter<fmt::string_view> {
    auto format(const NvSciBufAttrValColorFmt& nvsci_fmt, fmt::format_context& ctx) const -> decltype(ctx.out())
    {
#define FMT(f)           \
    case NvSciColor_##f: \
        return fmt::format_to(ctx.out(), "NvSciColor_" #f);
        switch (nvsci_fmt) {
            FMT(U8V8)
            FMT(U8_V8)
            FMT(V8U8)
            FMT(U10V10)
            FMT(V10U10)
            FMT(U12V12)
            FMT(V12U12)
            FMT(U16V16)
            FMT(V16U16)
            FMT(Y8)
            FMT(Y10)
            FMT(Y12)
            FMT(Y16)
            FMT(U8)
            FMT(U10)
            FMT(U12)
            FMT(U16)
            FMT(V8)
            FMT(V10)
            FMT(V12)
            FMT(V16)

            FMT(X2Rc10Rb10Ra10_Bayer10RGGB)
            FMT(X2Rc10Rb10Ra10_Bayer10BGGR)
            FMT(X2Rc10Rb10Ra10_Bayer10GRBG)
            FMT(X2Rc10Rb10Ra10_Bayer10GBRG)

            FMT(Bayer8RGGB)
            FMT(Bayer8CCCC)
            FMT(Bayer8BGGR)
            FMT(Bayer8GBRG)
            FMT(Bayer8GRBG)
            FMT(Bayer16BGGR)
            FMT(Bayer16CCCC)
            FMT(Bayer16GBRG)
            FMT(Bayer16GRBG)
            FMT(Bayer16RGGB)
            FMT(Bayer16RCCB)
            FMT(Bayer16BCCR)
            FMT(Bayer16CRBC)
            FMT(Bayer16CBRC)
            FMT(Bayer16RCCC)
            FMT(Bayer16CCCR)
            FMT(Bayer16CRCC)
            FMT(Bayer16CCRC)

            FMT(X2Bayer14GBRG)
            FMT(X4Bayer12GBRG)
            FMT(X6Bayer10GBRG)
            FMT(X2Bayer14GRBG)
            FMT(X4Bayer12GRBG)
            FMT(X6Bayer10GRBG)
            FMT(X2Bayer14BGGR)
            FMT(X4Bayer12BGGR)
            FMT(X6Bayer10BGGR)
            FMT(X2Bayer14RGGB)
            FMT(X4Bayer12RGGB)
            FMT(X6Bayer10RGGB)
            FMT(X2Bayer14CCCC)
            FMT(X4Bayer12CCCC)
            FMT(X6Bayer10CCCC)
            FMT(X4Bayer12RCCB)
            FMT(X4Bayer12BCCR)
            FMT(X4Bayer12CRBC)
            FMT(X4Bayer12CBRC)
            FMT(X4Bayer12RCCC)
            FMT(X4Bayer12CCCR)
            FMT(X4Bayer12CRCC)
            FMT(X4Bayer12CCRC)
            FMT(Signed_X2Bayer14CCCC)
            FMT(Signed_X4Bayer12CCCC)
            FMT(Signed_X6Bayer10CCCC)
            FMT(Signed_Bayer16CCCC)
            FMT(FloatISP_Bayer16CCCC)
            FMT(FloatISP_Bayer16RGGB)
            FMT(FloatISP_Bayer16BGGR)
            FMT(FloatISP_Bayer16GRBG)
            FMT(FloatISP_Bayer16GBRG)
            FMT(FloatISP_Bayer16RCCB)
            FMT(FloatISP_Bayer16BCCR)
            FMT(FloatISP_Bayer16CRBC)
            FMT(FloatISP_Bayer16CBRC)
            FMT(FloatISP_Bayer16RCCC)
            FMT(FloatISP_Bayer16CCCR)
            FMT(FloatISP_Bayer16CRCC)
            FMT(FloatISP_Bayer16CCRC)
            FMT(X12Bayer20CCCC)
            FMT(X12Bayer20BGGR)
            FMT(X12Bayer20RGGB)
            FMT(X12Bayer20GRBG)
            FMT(X12Bayer20GBRG)
            FMT(X12Bayer20RCCB)
            FMT(X12Bayer20BCCR)
            FMT(X12Bayer20CRBC)
            FMT(X12Bayer20CBRC)
            FMT(X12Bayer20RCCC)
            FMT(X12Bayer20CCCR)
            FMT(X12Bayer20CRCC)
            FMT(X12Bayer20CCRC)

            //FMT(Bayer16GBRG)
        default:
            throw std::runtime_error(fmt::format("Unknown NvSci format ({})", static_cast<int>(nvsci_fmt)));
        }
    }
};
