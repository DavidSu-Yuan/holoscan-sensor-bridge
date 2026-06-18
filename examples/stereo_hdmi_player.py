# SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# See README.md for detailed information.

import argparse
import ctypes
import logging
import os

import holoscan
from cuda import cuda

import hololink as hololink_module


class HoloscanApplication(holoscan.core.Application):
    def __init__(
        self,
        headless,
        cuda_context,
        cuda_device_ordinal,
        hololink_channel_left,
        ibv_name_left,
        ibv_port_left,
        hdmi_left,
        hololink_channel_right,
        ibv_name_right,
        ibv_port_right,
        hdmi_right,
        frame_limit,
        window_height,
        window_width,
        window_title,
    ):
        logging.info("__init__")
        super().__init__()
        self._headless = headless
        self._cuda_context = cuda_context
        self._cuda_device_ordinal = cuda_device_ordinal
        self._hololink_channel_left = hololink_channel_left
        self._ibv_name_left = ibv_name_left
        self._ibv_port_left = ibv_port_left
        self._hdmi_left = hdmi_left
        self._hololink_channel_right = hololink_channel_right
        self._ibv_name_right = ibv_name_right
        self._ibv_port_right = ibv_port_right
        self._hdmi_right = hdmi_right
        self._frame_limit = frame_limit
        self._window_height = window_height
        self._window_width = window_width
        self._window_title = window_title
        # These are HSDK controls-- because we have stereo
        # camera paths going into the same visualizer, don't
        # raise an error when each path present metadata
        # with the same names.  Because we don't use that metadata,
        # it's easiest to just ignore new items with the same
        # names as existing items.
        self.is_metadata_enabled = True
        self.metadata_policy = holoscan.core.MetadataPolicy.REJECT

    def compose(self):
        logging.info("compose")
        if self._frame_limit:
            self._count_left = holoscan.conditions.CountCondition(
                self,
                name="count_left",
                count=self._frame_limit,
            )
            condition_left = self._count_left
            self._count_right = holoscan.conditions.CountCondition(
                self,
                name="count_right",
                count=self._frame_limit,
            )
            condition_right = self._count_right
        else:
            self._ok_left = holoscan.conditions.BooleanCondition(
                self, name="ok_left", enable_tick=True
            )
            condition_left = self._ok_left
            self._ok_right = holoscan.conditions.BooleanCondition(
                self, name="ok_right", enable_tick=True
            )
            condition_right = self._ok_right

        hdmi_pool = holoscan.resources.BlockMemoryPool(
            self,
            name="pool",
            # storage_type of 1 is device memory
            storage_type=1,
            block_size=self._hdmi_left._width*3
            * self._hdmi_left._height,
            num_blocks=6,
        )
        hdmi_operator_left = hololink_module.operators.HDMIConverterOp(
            self,
            name="hdmi_operator_left",
            allocator=hdmi_pool,
            cuda_device_ordinal=self._cuda_device_ordinal,
            out_tensor_name="left",
        )
        self._hdmi_left.configure_converter(hdmi_operator_left)
        hdmi_operator_right = hololink_module.operators.HDMIConverterOp(
            self,
            name="hdmi_operator_right",
            allocator=hdmi_pool,
            cuda_device_ordinal=self._cuda_device_ordinal,
            out_tensor_name="right",
        )
        self._hdmi_right.configure_converter(hdmi_operator_right)

        frame_size = hdmi_operator_left.get_csi_length()
        assert frame_size == hdmi_operator_right.get_csi_length()
        frame_context = self._cuda_context
        receiver_operator_left = hololink_module.operators.RoceReceiverOp(
            self,
            condition_left,
            name="receiver_left",
            frame_size=frame_size,
            frame_context=frame_context,
            ibv_name=self._ibv_name_left,
            ibv_port=self._ibv_port_left,
            hololink_channel=self._hololink_channel_left,
            device=self._hdmi_left,
        )

        #
        receiver_operator_right = hololink_module.operators.RoceReceiverOp(
            self,
            condition_right,
            frame_size=frame_size,
            frame_context=frame_context,
            ibv_name=self._ibv_name_right,
            ibv_port=self._ibv_port_right,
            hololink_channel=self._hololink_channel_right,
            device=self._hdmi_right,
        )

        left_spec = holoscan.operators.HolovizOp.InputSpec(
            "left", holoscan.operators.HolovizOp.InputType.COLOR
        )
        left_spec_view = holoscan.operators.HolovizOp.InputSpec.View()
        left_spec_view.offset_x = 0
        left_spec_view.offset_y = 0
        left_spec_view.width = 0.5
        left_spec_view.height = 1
        left_spec.views = [left_spec_view]

        right_spec = holoscan.operators.HolovizOp.InputSpec(
            "right", holoscan.operators.HolovizOp.InputType.COLOR
        )
        right_spec_view = holoscan.operators.HolovizOp.InputSpec.View()
        right_spec_view.offset_x = 0.5
        right_spec_view.offset_y = 0
        right_spec_view.width = 0.5
        right_spec_view.height = 1
        right_spec.views = [right_spec_view]

        visualizer = holoscan.operators.HolovizOp(
            self,
            name="holoviz",
            headless=self._headless,
            framebuffer_srgb=True,
            tensors=[left_spec, right_spec],
            height=self._window_height,
            width=self._window_width,
            window_title=self._window_title,
        )
        #
        self.add_flow(receiver_operator_left, hdmi_operator_left, {("output", "input")})
        self.add_flow(receiver_operator_right, hdmi_operator_right, {("output", "input")})
        self.add_flow(hdmi_operator_left, visualizer, {("output", "receivers")})
        self.add_flow(hdmi_operator_right, visualizer, {("output", "receivers")})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--headless", action="store_true", help="Run in headless mode")
    parser.add_argument(
        "--fullscreen", action="store_true", help="Run in fullscreen mode"
    )
    parser.add_argument(
        "--disable_i2c", action="store_true", help="Disable i2c for testing"
    )
    parser.add_argument(
        "--video_4k", action="store_true", help="Force to 4k"
    )
    parser.add_argument(
        "--frame-limit",
        type=int,
        default=None,
        help="Exit after receiving this many frames",
    )
    default_configuration = os.path.join(
        os.path.dirname(__file__), "example_configuration.yaml"
    )
    parser.add_argument(
        "--configuration",
        default=default_configuration,
        help="Configuration file",
    )
    parser.add_argument(
        "--hololink-left",
        default="192.168.0.2",
        help="IP address of Hololink board",
    )
    parser.add_argument(
        "--hololink-right",
        default="192.168.0.3",
        help="IP address of Hololink board",
    )
    parser.add_argument(
        "--log-level",
        type=int,
        default=20,
        help="Logging level to display",
    )
    infiniband_devices = hololink_module.infiniband_devices()
    parser.add_argument(
        "--ibv-name-left",
        default=infiniband_devices[0],
        help="IBV device to use",
    )
    parser.add_argument(
        "--ibv-port-left",
        type=int,
        default=1,
        help="Port number of IBV device",
    )
    parser.add_argument(
        "--ibv-name-right",
        default=infiniband_devices[1],
        help="IBV device to use",
    )
    parser.add_argument(
        "--ibv-port-right",
        type=int,
        default=1,
        help="Port number of IBV device",
    )
    parser.add_argument(
        "--window-height",
        type=int,
        default=2160 // 8,  # arbitrary default
        help="Set the height of the displayed window",
    )
    parser.add_argument(
        "--window-width",
        type=int,
        default=3840 // 6,  # arbitrary default
        help="Set the width of the displayed window",
    )
    parser.add_argument(
        "--title",
        help="Set the window title",
    )
    args = parser.parse_args()
    hololink_module.logging_level(args.log_level)
    logging.info("Initializing.")
    # Get a handle to the GPU
    (cu_result,) = cuda.cuInit(0)
    assert cu_result == cuda.CUresult.CUDA_SUCCESS
    cu_device_ordinal = 0
    cu_result, cu_device = cuda.cuDeviceGet(cu_device_ordinal)
    assert cu_result == cuda.CUresult.CUDA_SUCCESS
    cu_result, cu_context = cuda.cuDevicePrimaryCtxRetain(cu_device)
    assert cu_result == cuda.CUresult.CUDA_SUCCESS
    # Get a handle to data sources
    channel_metadata_left = hololink_module.Enumerator.find_channel(
        channel_ip=args.hololink_left
    )
    hololink_channel_left = hololink_module.DataChannel(channel_metadata_left)
    channel_metadata_right = hololink_module.Enumerator.find_channel(
        channel_ip=args.hololink_right
    )
    hololink_channel_right = hololink_module.DataChannel(channel_metadata_right)
    # Get a handle to the camera
    hdmi_left = hololink_module.sensors.HDMISource(hololink_channel_left, 0, args.disable_i2c)
    if args.video_4k:
        logging.info("Force to 4k")
        hdmi_left._width = 3840
        hdmi_left._height = 2160
    hdmi_right = hololink_module.sensors.HDMISource(hololink_channel_left, 0, args.disable_i2c)
    if args.video_4k:
        logging.info("Force to 4k")
        hdmi_right._width = 3840
        hdmi_right._height = 2160
    # What title should we use?
    window_title = f"Holoviz - {args.hololink_left}"
    if args.title is not None:
        window_title = args.title
    # Set up the application
    application = HoloscanApplication(
        args.headless,
        cu_context,
        cu_device_ordinal,
        hololink_channel_left,
        args.ibv_name_left,
        args.ibv_port_left,
        hdmi_left,
        hololink_channel_right,
        args.ibv_name_right,
        args.ibv_port_right,
        hdmi_right,
        args.frame_limit,
        args.window_height,
        args.window_width,
        window_title,
    )
    application.config(args.configuration)
    # Run it.
    hololink = hololink_channel_left.hololink()
    assert hololink is hololink_channel_right.hololink()
    hololink.start()
    hololink.reset()

    # For demonstration purposes, use the Event based
    # scheduler.  Any HSDK scheduler works fine here,
    # including the default greedy scheduler, which
    # you get if you don't explicitly configure one.
    scheduler = holoscan.schedulers.EventBasedScheduler(
        application,
        worker_thread_number=4,
        name="event_scheduler",
    )
    application.scheduler(scheduler)

    application.run()
    hololink.stop()

    (cu_result,) = cuda.cuDevicePrimaryCtxRelease(cu_device)
    assert cu_result == cuda.CUresult.CUDA_SUCCESS


if __name__ == "__main__":
    main()
