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

#include "hdmi_converter.hpp"

#include <hololink/common/cuda_helper.hpp>
#include <hololink/core/logging_internal.hpp>
#include <hololink/core/networking.hpp>

namespace {

const char* source = R"(
extern "C" {


__global__ void LineByLine2TopBottom(const uchar3* inputImage,
                             uchar3* outputImage, int width, int height,
                             int bottom_offset) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) return;

    int idx = y * width + x;

    if (y % 2 == 0) {
        outputImage[(y/2) * width + x] = inputImage[idx];
    } else {
        outputImage[(y/2 + bottom_offset) * width + x] = inputImage[idx];
    }
}

__global__ void SeparateEyes(const uchar3* inputImage,
                             uchar3* leftEye, uchar3* rightEye,
                             int width, int height) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) return;

    int idx = y * width + x;

    if (y % 2 == 0) {
        leftEye[(y/2) * width + x] = inputImage[idx];
    } else {
        rightEye[(y/2) * width + x] = inputImage[idx];
    }
}

__global__ void StitchSideBySide(const uchar3* leftEye, const uchar3* rightEye,
                                 uchar3* outputImage, int width, int height) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) return;

    int idx = y * width + x;

    // Left eye
    outputImage[y * width * 2 + x] = leftEye[idx];
    // Right eye
    outputImage[y * width * 2 + width + x] = rightEye[idx];
}

__global__ void StitchTopBottom(const uchar3* leftEye, const uchar3* rightEye,
                                 uchar3* outputImage, int width, int height,
                                 int bottom_offset) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) return;

    int idx = y * width + x;

    // Left eye
    outputImage[y * width + x] = leftEye[idx];
    // Right eye
    outputImage[(y + bottom_offset) * width + x] = rightEye[idx];
}

__device__ void YUVtoRGB(float Y, float U, float V, uchar3 &rgb) {
    float r = Y + 1.402f * (V - 128);
    float g = Y - 0.344136f * (U - 128) - 0.714136f * (V - 128);
    float b = Y + 1.772f * (U - 128);

    rgb.x = min(max(r, 0.0f), 255.0f);  // R
    rgb.y = min(max(g, 0.0f), 255.0f);  // G
    rgb.z = min(max(b, 0.0f), 255.0f);  // B
}

__global__ void YUY2ToRGBKernel(uchar3* rgb,
                                const uchar2* yuy2,
                                int pitch,
                                int width,
                                int height) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x < width / 2 && y < height) {
        int index = y * (pitch / 2) + x;
        uchar2 yuyv1 = yuy2[index];
        uchar2 yuyv2 = yuy2[index + 1];

        float Y0 = static_cast<float>(yuyv1.x);
        float U  = static_cast<float>(yuyv1.y);
        float Y1 = static_cast<float>(yuyv2.x);
        float V  = static_cast<float>(yuyv2.y);

        int rgbIndex = y * width + (x * 2);
        uchar3 pixel1, pixel2;

        YUVtoRGB(Y0, U, V, pixel1);
        YUVtoRGB(Y1, U, V, pixel2);

        rgb[rgbIndex] = pixel1;
        rgb[rgbIndex + 1] = pixel2;
    }
}


})";

} // anonymous namespace

namespace hololink::common {

class CudaFunctionLauncher;

} // namespace hololink::common

namespace hololink::operators {

static inline size_t align_8(size_t value) { return (value + 7) & ~7; }

void HDMIConverterOp::setup(holoscan::OperatorSpec& spec)
{
    spec.input<holoscan::gxf::Entity>("input");

    spec.param(allocator_, "allocator", "Allocator",
        "Allocator used to allocate the output Bayer image, defaults to BlockMemoryPool");
    spec.param(cuda_device_ordinal_, "cuda_device_ordinal", "CudaDeviceOrdinal",
        "Device to use for CUDA operations", 0);
    spec.param(out_tensor_name_, "out_tensor_name", "OutputTensorName",
        "Name of the output tensor", std::string(""));
    spec.param(left_tensor_name_, "left_tensor_name", "LeftTensorName",
        "Name of the left eye tensor", std::string(""));
    spec.param(right_tensor_name_, "left_tensor_name", "RightTensorName",
        "Name of the right eye tensor", std::string(""));

    spec.param(input_3d_format_, "input_3d_format", "Input3DFormat", "3D format of Input", 0);
    spec.param(output_3d_format_, "output_3d_format", "Output3DFormat", "3D format of Output", 0);
    spec.param(separate_3d_buffer_, "separate_3d_buffer", "Separate3DBuffer", "Separate 3D buffer", 0);

    spec.output<holoscan::gxf::Entity>("output");

    cuda_stream_handler_.define_params(spec);
}

void HDMIConverterOp::start()
{
    HSB_LOG_INFO("HDMI converter start");
    if (!configured_) {
        throw std::runtime_error("HDMIConverterOp is not configured.");
    }

    CudaCheck(cuInit(0));
    CudaCheck(cuDeviceGet(&cuda_device_, cuda_device_ordinal_.get()));
    CudaCheck(cuDevicePrimaryCtxRetain(&cuda_context_, cuda_device_));
    int integrated = 0;
    CudaCheck(cuDeviceGetAttribute(&integrated, CU_DEVICE_ATTRIBUTE_INTEGRATED, cuda_device_));
    is_integrated_ = (integrated != 0);

    hololink::common::CudaContextScopedPush cur_cuda_context(cuda_context_);

    cuda_function_launcher_.reset(new hololink::common::CudaFunctionLauncher(
        source, { "YUY2ToRGBKernel", "LineByLine2TopBottom", "SeparateEyes", "StitchSideBySide", "StitchTopBottom"}));

    left_eye_buffer_ = std::make_unique<nvidia::gxf::MemoryBuffer>();
    right_eye_buffer_ = std::make_unique<nvidia::gxf::MemoryBuffer>();
    left_eye_resize_buffer_ = std::make_unique<nvidia::gxf::MemoryBuffer>();
    right_eye_resize_buffer_ = std::make_unique<nvidia::gxf::MemoryBuffer>();
    device_scratch_buffer_ = std::make_unique<nvidia::gxf::MemoryBuffer>();
}

void HDMIConverterOp::stop()
{
    HSB_LOG_INFO("HDMI converter stop");
    hololink::common::CudaContextScopedPush cur_cuda_context(cuda_context_);

    left_eye_buffer_->freeBuffer();
    right_eye_buffer_->freeBuffer();
    left_eye_resize_buffer_->freeBuffer();
    right_eye_resize_buffer_->freeBuffer();
    device_scratch_buffer_->freeBuffer();

    cuda_function_launcher_.reset();

    CudaCheck(cuDevicePrimaryCtxRelease(cuda_device_));
    cuda_context_ = nullptr;
}

std::string nppStatusToStr(NppStatus status) {
    switch (status) {
        case NPP_SUCCESS: return "NPP_SUCCESS";
        case NPP_NULL_POINTER_ERROR: return "NPP_NULL_POINTER_ERROR";
        case NPP_INTERPOLATION_ERROR: return "NPP_INTERPOLATION_ERROR";
        case NPP_SIZE_ERROR: return "NPP_SIZE_ERROR";
        case NPP_STEP_ERROR: return "NPP_STEP_ERROR";
        case NPP_MEMORY_ALLOCATION_ERR: return "NPP_MEMORY_ALLOCATION_ERR";
        case NPP_NOT_SUPPORTED_MODE_ERROR: return "NPP_NOT_SUPPORTED_MODE_ERROR";
        case NPP_RESIZE_NO_OPERATION_ERROR: return "NPP_RESIZE_NO_OPERATION_ERROR";
        case NPP_OUT_OFF_RANGE_ERROR: return "NPP_OUT_OFF_RANGE_ERROR";
        default:
            return fmt::format("Unknown NppStatus ({})", static_cast<int>(status));
    }
}

std::string video3dformatToStr(int format) {
    switch (format) {
        case HDMIConverterOp::INVALID: return "INVALID";
        case HDMIConverterOp::FRAME_PACKING: return "FRAME_PACKING";
        case HDMIConverterOp::SIDE_BY_SIDE_HALF: return "SIDE_BY_SIDE_HALF";
        case HDMIConverterOp::TOP_AND_BOTTOM: return "TOP_AND_BOTTOM";
        case HDMIConverterOp::LINE_BY_LINE: return "LINE_BY_LINE";
        case HDMIConverterOp::FIELD_ALTERNATIVE: return "FIELD_ALTERNATIVE";
        case HDMIConverterOp::VIDEO_PLUS_DEPTH: return "2D_PLUS_DEPTH";
        case HDMIConverterOp::SIDE_BY_SIDE_FULL: return "SIDE_BY_SIDE_FULL";

        default:
            return "UNKNOWN_FORMAT";
    };
}

static void resize_rgb_buffer_by_npp(
        const unsigned char *pSrc, int srcWidth, int srcHeight,
        unsigned char *pDst, int dstWidth, int dstHeight, const cudaStream_t cuda_stream) {
    NppStatus status = NPP_SUCCESS;
    NppiSize oSrcSize = {srcWidth, srcHeight};
    NppiRect oSrcRectROI = {0, 0, srcWidth, srcHeight};
    NppiSize oDstSize = {dstWidth, dstHeight};
    NppiRect oDstRectROI = {0, 0, dstWidth, dstHeight};
    cudaStream_t npp_stream = nppGetStream();
    if (npp_stream != cuda_stream) {
        status = nppSetStream(cuda_stream);
        if (status != NPP_SUCCESS) {
            throw std::runtime_error(fmt::format("Failed to set stream"));
        }
    }

    // HSB_LOG_WARN(fmt::format("{} {} {} to {} {} {}",
    //            static_cast<const void*>(pSrc), srcWidth, srcHeight,
    //            static_cast<void*>(pDst), dstWidth, dstHeight));

    status = nppiResize_8u_C3R(
            pSrc, srcWidth * 3, oSrcSize, oSrcRectROI,
            pDst, dstWidth * 3, oDstSize, oDstRectROI, NPPI_INTER_LINEAR);
    if (status != NPP_SUCCESS) {
        throw std::runtime_error(fmt::format("Failed to resize buffer: {}", nppStatusToStr(status)));
    }
}

void HDMIConverterOp::compute(holoscan::InputContext& input, holoscan::OutputContext& output,
    holoscan::ExecutionContext& context)
{
    auto maybe_entity = input.receive<holoscan::gxf::Entity>("input");
    if (!maybe_entity) {
        throw std::runtime_error("Failed to receive input");
    }

    auto& entity = static_cast<nvidia::gxf::Entity&>(maybe_entity.value());

    // get the CUDA stream from the input message
    gxf_result_t stream_handler_result
        = cuda_stream_handler_.from_message(context.context(), entity);
    if (stream_handler_result != GXF_SUCCESS) {
        throw std::runtime_error(fmt::format("Failed to get the CUDA stream from incoming messages: {}", GxfResultStr(stream_handler_result)));
    }

    const auto maybe_tensor = entity.get<nvidia::gxf::Tensor>();
    if (!maybe_tensor) {
        throw std::runtime_error("Tensor not found in message");
    }

    const auto input_tensor = maybe_tensor.value();

    if (input_tensor->storage_type() == nvidia::gxf::MemoryStorageType::kHost) {
        if (!is_integrated_ && !host_memory_warning_) {
            host_memory_warning_ = true;
            HSB_LOG_WARN(
                "The input tensor is stored in host memory, this will reduce performance of this "
                "operator. For best performance store the input tensor in device memory.");
        }
    } else if (input_tensor->storage_type() != nvidia::gxf::MemoryStorageType::kDevice) {
        throw std::runtime_error(
            fmt::format("Unsupported storage type {}", (int)input_tensor->storage_type()));
    }

    if (input_tensor->rank() != 1) {
        throw std::runtime_error("Tensor must be one dimensional");
    }

    const int32_t size = input_tensor->shape().dimension(0);

    // get handle to underlying nvidia::gxf::Allocator from std::shared_ptr<holoscan::Allocator>
    auto allocator = nvidia::gxf::Handle<nvidia::gxf::Allocator>::Create(
        fragment()->executor().context(), allocator_->gxf_cid());

    size_t shape_width = pixel_width_;
    size_t shape_height = pixel_height_;
    size_t shape_channels = 1;
    nvidia::gxf::PrimitiveType primitive_type = nvidia::gxf::PrimitiveType::kUnsigned16;
    // create the output
    if (pixel_format_ == hololink::csi::PixelFormat::YUYV_8) {
        shape_width = pixel_width_ ;
        shape_height = pixel_height_;
        shape_channels = 3;
        primitive_type = nvidia::gxf::PrimitiveType::kUnsigned8;
    }

    nvidia::gxf::Shape shape { int(shape_height), int(shape_width), int(shape_channels) };
    nvidia::gxf::Expected<nvidia::gxf::Entity> out_message =
        CreateTensorMap(context.context(), allocator.value(),
                { { out_tensor_name_.get(), nvidia::gxf::MemoryStorageType::kDevice, shape,
                primitive_type, 0, nvidia::gxf::ComputeTrivialStrides(shape,
                        nvidia::gxf::PrimitiveTypeSize(primitive_type)) } },
                false);

    if (!out_message) {
        throw std::runtime_error("failed to create out_message");
    }
    const auto tensor = out_message.value().get<nvidia::gxf::Tensor>(out_tensor_name_.get().c_str());
    if (!tensor) {
        throw std::runtime_error(
            fmt::format("failed to create out_tensor with name \"{}\"", out_tensor_name_.get()));
    }

    hololink::common::CudaContextScopedPush cur_cuda_context(cuda_context_);
    const cudaStream_t cuda_stream = cuda_stream_handler_.get_cuda_stream(context.context());

    size_t scratch_buffer_size =  shape_width * shape_height * shape_channels;
    if (scratch_buffer_size > device_scratch_buffer_->size()) {
        device_scratch_buffer_->resize(
                allocator.value(), scratch_buffer_size, nvidia::gxf::MemoryStorageType::kDevice);
        if (!device_scratch_buffer_->pointer()) {
            throw std::runtime_error(
                    fmt::format("Failed to allocate device scratch buffer ({} bytes)", scratch_buffer_size));
        }
    }

    if (input_3d_format_ == INVALID && output_3d_format_ == INVALID) {
        cudaStream_t npp_stream = nppGetStream();
        NppStatus status = NPP_SUCCESS;
        NppiSize oSizeROI;
        oSizeROI.width = pixel_width_;
        oSizeROI.height = pixel_height_;
        switch (pixel_format_) {
            case hololink::csi::PixelFormat::YUYV_8:
                if (npp_stream != cuda_stream)
                    status = nppSetStream(cuda_stream);
                if (status != NPP_SUCCESS) {
                    throw std::runtime_error(fmt::format("Failed to set npp stream"));
                }
                status = nppiYCbCr422ToRGB_8u_C2C3R(
                        input_tensor->pointer() + start_byte_,
                        bytes_per_line_,
                        tensor.value()->pointer(),
                        pixel_width_*3,
                        oSizeROI);
                if (status != NPP_SUCCESS) {
                    throw std::runtime_error(fmt::format("Failed to convert to rgb"));
                }
                break;
            default:
                throw std::runtime_error("Unsupported bits per pixel value");
        }
    } else if (input_3d_format_ == LINE_BY_LINE && output_3d_format_ == SIDE_BY_SIDE_HALF) {
        cudaStream_t npp_stream = nppGetStream();
        NppStatus status = NPP_SUCCESS;
        NppiSize oSizeROI;
        oSizeROI.width = pixel_width_;
        oSizeROI.height = pixel_height_;
        switch (pixel_format_) {
            case hololink::csi::PixelFormat::YUYV_8:
                if (npp_stream != cuda_stream)
                    status = nppSetStream(cuda_stream);
                if (status != NPP_SUCCESS) {
                    throw std::runtime_error(fmt::format("Failed to set npp stream"));
                }
                status = nppiYCbCr422ToRGB_8u_C2C3R(
                        input_tensor->pointer() + start_byte_,
                        bytes_per_line_,
                        device_scratch_buffer_->pointer(),
                        pixel_width_*3,
                        oSizeROI);
                if (status != NPP_SUCCESS) {
                    throw std::runtime_error(fmt::format("Failed to convert to rgb"));
                }
                break;
            default:
                throw std::runtime_error("Unsupported bits per pixel value");
        }

        hololink::common::CudaContextScopedPush cur_cuda_context(cuda_context_);
        const cudaStream_t cuda_stream = cuda_stream_handler_.get_cuda_stream(context.context());

        size_t eye_buffer_size = (shape_width / 2) * (shape_height) * shape_channels;

        // left eye
        if (eye_buffer_size > left_eye_buffer_->size()) {
            left_eye_buffer_->resize(
                    allocator.value(), eye_buffer_size, nvidia::gxf::MemoryStorageType::kDevice);
            if (!left_eye_buffer_->pointer()) {
                throw std::runtime_error(
                        fmt::format("Failed to allocate buffer ({} bytes)", eye_buffer_size));
            }
        }

        // right eye
        if (eye_buffer_size > right_eye_buffer_->size()) {
            right_eye_buffer_->resize(
                    allocator.value(), eye_buffer_size, nvidia::gxf::MemoryStorageType::kDevice);
            if (!right_eye_buffer_->pointer()) {
                throw std::runtime_error(
                        fmt::format("Failed to allocate buffer ({} bytes)", eye_buffer_size));
            }
        }

        cuda_function_launcher_->launch("SeparateEyes",
                { (unsigned int)shape_width, (unsigned int)shape_height, 1 }, cuda_stream,
                device_scratch_buffer_->pointer(),
                left_eye_buffer_->pointer(), right_eye_buffer_->pointer(),
                shape_width, shape_height);

        // left eye
        if (eye_buffer_size > left_eye_resize_buffer_->size()) {
            left_eye_resize_buffer_->resize(
                    allocator.value(), eye_buffer_size, nvidia::gxf::MemoryStorageType::kDevice);
            if (!left_eye_resize_buffer_->pointer()) {
                throw std::runtime_error(
                        fmt::format("Failed to allocate buffer ({} bytes)", eye_buffer_size));
            }
        }

        // right eye
        if (eye_buffer_size > right_eye_resize_buffer_->size()) {
            right_eye_resize_buffer_->resize(
                    allocator.value(), eye_buffer_size, nvidia::gxf::MemoryStorageType::kDevice);
            if (!right_eye_resize_buffer_->pointer()) {
                throw std::runtime_error(
                        fmt::format("Failed to allocate buffer ({} bytes)", eye_buffer_size));
            }
        }

        //cudaMemset(left_eye_buffer_->pointer(), 128, shape_width* 3 * shape_height / 2);
        //cudaMemset(right_eye_buffer_->pointer(), 256, shape_width* 3 * shape_height / 2);
        if (separate_3d_buffer_.get() == 0) {
            resize_rgb_buffer_by_npp(
                    left_eye_buffer_->pointer(), pixel_width_, pixel_height_ / 2,
                    left_eye_resize_buffer_->pointer(), pixel_width_ / 2, pixel_height_, cuda_stream);

            resize_rgb_buffer_by_npp(
                    right_eye_buffer_->pointer(), pixel_width_, pixel_height_ / 2,
                    right_eye_resize_buffer_->pointer(), pixel_width_ / 2, pixel_height_, cuda_stream);

            //cudaMemset(left_eye_resize_buffer_->pointer(), 128, shape_width* 3 * shape_height / 2);
            //cudaMemset(left_eye_resize_buffer_->pointer(), 256, shape_width* 3 * shape_height / 2);

            cuda_function_launcher_->launch("StitchSideBySide",
                    { (unsigned int)shape_width / 2, (unsigned int)shape_height, 1 }, cuda_stream,
                    left_eye_resize_buffer_->pointer(), right_eye_resize_buffer_->pointer(),
                    tensor.value()->pointer(),
                    shape_width / 2, shape_height);
        } else {
        #if 0
            nvidia::gxf::Expected<nvidia::gxf::Entity> left_message =
                CreateTensorMap(context.context(), allocator.value(),
                        { { left_tensor_name_.get(), nvidia::gxf::MemoryStorageType::kDevice, shape,
                        primitive_type, 0, nvidia::gxf::ComputeTrivialStrides(shape,
                                nvidia::gxf::PrimitiveTypeSize(primitive_type)) } },
                        false);

            if (!left_message) {
                throw std::runtime_error("failed to create left_message");
            }

            nvidia::gxf::Expected<nvidia::gxf::Entity> right_message =
                CreateTensorMap(context.context(), allocator.value(),
                        { { right_tensor_name_.get(), nvidia::gxf::MemoryStorageType::kDevice, shape,
                        primitive_type, 0, nvidia::gxf::ComputeTrivialStrides(shape,
                                nvidia::gxf::PrimitiveTypeSize(primitive_type)) } },
                        false);

            if (!right_message) {
                throw std::runtime_error("failed to create right_message");
            }
        #endif

            const auto left_tensor = out_message.value().get<nvidia::gxf::Tensor>(left_tensor_name_.get().c_str());
            if (!tensor) {
                throw std::runtime_error(
                        fmt::format("failed to create out_tensor with name \"{}\"", left_tensor_name_.get()));
            }

            const auto right_tensor = out_message.value().get<nvidia::gxf::Tensor>(right_tensor_name_.get().c_str());
            if (!tensor) {
                throw std::runtime_error(
                        fmt::format("failed to create out_tensor with name \"{}\"", right_tensor_name_.get()));
            }

            resize_rgb_buffer_by_npp(
                    left_eye_buffer_->pointer(), pixel_width_, pixel_height_ / 2,
                    left_tensor.value()->pointer(), pixel_width_ / 2, pixel_height_, cuda_stream);

            resize_rgb_buffer_by_npp(
                    right_eye_buffer_->pointer(), pixel_width_, pixel_height_ / 2,
                    right_tensor.value()->pointer(), pixel_width_ / 2, pixel_height_, cuda_stream);
        }
    } else if (input_3d_format_ == LINE_BY_LINE && output_3d_format_ == TOP_AND_BOTTOM) {
        cudaStream_t npp_stream = nppGetStream();
        NppStatus status = NPP_SUCCESS;
        NppiSize oSizeROI;
        oSizeROI.width = pixel_width_;
        oSizeROI.height = pixel_height_;
        switch (pixel_format_) {
            case hololink::csi::PixelFormat::YUYV_8:
                if (npp_stream != cuda_stream)
                    status = nppSetStream(cuda_stream);
                if (status != NPP_SUCCESS) {
                    throw std::runtime_error(fmt::format("Failed to set npp stream"));
                }
                status = nppiYCbCr422ToRGB_8u_C2C3R(
                        input_tensor->pointer() + start_byte_,
                        bytes_per_line_,
                        device_scratch_buffer_->pointer(),
                        pixel_width_*3,
                        oSizeROI);
                if (status != NPP_SUCCESS) {
                    throw std::runtime_error(fmt::format("Failed to convert to rgb"));
                }
                break;
            default:
                throw std::runtime_error("Unsupported bits per pixel value");
        }

        hololink::common::CudaContextScopedPush cur_cuda_context(cuda_context_);
        const cudaStream_t cuda_stream = cuda_stream_handler_.get_cuda_stream(context.context());

        if (separate_3d_buffer_.get() == 0) {
            cuda_function_launcher_->launch("LineByLine2TopBottom",
                    { (unsigned int)shape_width, (unsigned int)shape_height, 1 }, cuda_stream,
                    device_scratch_buffer_->pointer(),
                    tensor.value()->pointer(),
                    shape_width, shape_height, shape_height / 2 - 1);
        } else {
        #if 0
            nvidia::gxf::Expected<nvidia::gxf::Entity> left_message =
                CreateTensorMap(context.context(), allocator.value(),
                        { { left_tensor_name_.get(), nvidia::gxf::MemoryStorageType::kDevice, shape,
                        primitive_type, 0, nvidia::gxf::ComputeTrivialStrides(shape,
                                nvidia::gxf::PrimitiveTypeSize(primitive_type)) } },
                        false);

            if (!left_message) {
                throw std::runtime_error("failed to create left_message");
            }

            nvidia::gxf::Expected<nvidia::gxf::Entity> right_message =
                CreateTensorMap(context.context(), allocator.value(),
                        { { right_tensor_name_.get(), nvidia::gxf::MemoryStorageType::kDevice, shape,
                        primitive_type, 0, nvidia::gxf::ComputeTrivialStrides(shape,
                                nvidia::gxf::PrimitiveTypeSize(primitive_type)) } },
                        false);

            if (!right_message) {
                throw std::runtime_error("failed to create right_message");
            }
        #endif

            const auto left_tensor = out_message.value().get<nvidia::gxf::Tensor>(left_tensor_name_.get().c_str());
            if (!tensor) {
                throw std::runtime_error(
                        fmt::format("failed to create out_tensor with name \"{}\"", left_tensor_name_.get()));
            }

            const auto right_tensor = out_message.value().get<nvidia::gxf::Tensor>(right_tensor_name_.get().c_str());
            if (!tensor) {
                throw std::runtime_error(
                        fmt::format("failed to create out_tensor with name \"{}\"", right_tensor_name_.get()));
            }

            HSB_LOG_INFO("Not yet implement");
        }
    } else {
        throw std::runtime_error(
                fmt::format("Failed to convert 3d format {} to {}",
                    video3dformatToStr(input_3d_format_.get()), video3dformatToStr(output_3d_format_.get())));
    }

    // pass the CUDA stream to the output message
    stream_handler_result = cuda_stream_handler_.to_message(out_message);
    if (stream_handler_result != GXF_SUCCESS) {
        throw std::runtime_error("Failed to add the CUDA stream to the outgoing messages");
    }

    // Emit the tensor
    auto result = holoscan::gxf::Entity(std::move(out_message.value()));
    output.emit(result);
}

uint32_t HDMIConverterOp::receiver_start_byte()
{
    // HSB, in this mode, doesn't insert any stuff in the front of received data.
    return 0;
}

uint32_t HDMIConverterOp::received_line_bytes(uint32_t transmitted_line_bytes)
{
    // Bytes are padded to 8.
    return hololink::core::round_up(transmitted_line_bytes, 8);
}

uint32_t HDMIConverterOp::transmitted_line_bytes(hololink::csi::PixelFormat pixel_format, uint32_t pixel_width)
{
    switch (pixel_format) {
    case hololink::csi::PixelFormat::YUYV_8:
        return pixel_width * 2;
    default:
        throw std::runtime_error(fmt::format("Unsupported pixel format {}", int(pixel_format)));
    }
}

void HDMIConverterOp::configure(uint32_t start_byte, uint32_t bytes_per_line, uint32_t pixel_width, uint32_t pixel_height, hololink::csi::PixelFormat pixel_format, uint32_t trailing_bytes)
{
    HSB_LOG_INFO("start_byte={}, bytes_per_line={}, pixel_width={}, pixel_height={}, pixel_format={}, trailing_bytes={}.",
        start_byte, bytes_per_line, pixel_width, pixel_height, static_cast<int>(pixel_format), trailing_bytes);
    start_byte_ = start_byte;
    bytes_per_line_ = bytes_per_line;
    pixel_width_ = pixel_width;
    pixel_height_ = pixel_height;
    pixel_format_ = pixel_format;
    csi_length_ = start_byte + bytes_per_line * pixel_height + trailing_bytes;
    configured_ = true;
}

size_t HDMIConverterOp::get_csi_length()
{
    if (!configured_) {
        throw std::runtime_error("CsiToBayerOp is not configured.");
    }

    return csi_length_;
}

} // namespace hololink::operators
