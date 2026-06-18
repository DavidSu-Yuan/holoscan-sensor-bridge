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
        fullscreen,
        convert_3d,
        cuda_context,
        cuda_device_ordinal,
        hololink_channel,
        camera,
        frame_limit,
    ):
        logging.info("__init__")
        super().__init__()
        self._headless = headless
        self._fullscreen = fullscreen
        self._convert_3d = convert_3d
        self._cuda_context = cuda_context
        self._cuda_device_ordinal = cuda_device_ordinal
        self._hololink_channel = hololink_channel
        self._camera = camera
        self._frame_limit = frame_limit

    def compose(self):
        logging.info("compose")
        if self._frame_limit:
            self._count = holoscan.conditions.CountCondition(
                self,
                name="count",
                count=self._frame_limit,
            )
            condition = self._count
        else:
            self._ok = holoscan.conditions.BooleanCondition(
                self, name="ok", enable_tick=True
            )
            condition = self._ok

        hdmi_converter_pool = holoscan.resources.BlockMemoryPool(
            self,
            name="pool",
            # storage_type of 1 is device memory
            storage_type=1,
            block_size=self._camera._width*3
            * ctypes.sizeof(ctypes.c_uint8)
            * self._camera._height,
            num_blocks=9, # for 3d convert need more buffer
        )

        if self._convert_3d == 0: # No 3D format convert
            hdmi_converter_operator = hololink_module.operators.HDMIConverterOp(
                self,
                name="hdmi_converter",
                allocator=hdmi_converter_pool,
                cuda_device_ordinal=self._cuda_device_ordinal)
        elif self._convert_3d == 1: # Convert from line_by_line to side_by_side_half
            hdmi_converter_operator = hololink_module.operators.HDMIConverterOp(
                self,
                name="hdmi_converter",
                allocator=hdmi_converter_pool,
                cuda_device_ordinal=self._cuda_device_ordinal,
                input_3d_format=hololink_module.operators.HDMIConverterOp.Video3DFormat.LINE_BY_LINE,
                output_3d_format=hololink_module.operators.HDMIConverterOp.Video3DFormat.SIDE_BY_SIDE_HALF)
        elif self._convert_3d == 2: # Convert from line_by_line to side_by_side_half
            hdmi_converter_operator = hololink_module.operators.HDMIConverterOp(
                self,
                name="hdmi_converter",
                allocator=hdmi_converter_pool,
                cuda_device_ordinal=self._cuda_device_ordinal,
                input_3d_format=hololink_module.operators.HDMIConverterOp.Video3DFormat.LINE_BY_LINE,
                output_3d_format=hololink_module.operators.HDMIConverterOp.Video3DFormat.TOP_AND_BOTTOM)
        self._camera.configure_converter(hdmi_converter_operator)

        #self._camera.start();
        #try:
        #    for i in range(0, 100):
        #        value = self._camera.get_register(i);
        #        print(f"value {i} {value}")
        #except Exception as e:
        #    print("get register error: ", e)
        #    traceback.print_exc()

        frame_size = hdmi_converter_operator.get_csi_length()
        logging.info(f"{frame_size=}")
        frame_context = self._cuda_context
        receiver_operator = hololink_module.operators.LinuxReceiverOperator(
            self,
            condition,
            name="receiver",
            frame_size=frame_size,
            frame_context=frame_context,
            hololink_channel=self._hololink_channel,
            device=self._camera,
        )

        visualizer_args = self.kwargs("holoviz")
        visualizer = holoscan.operators.HolovizOp(
                self,
                name="holoviz",
                fullscreen=self._fullscreen,
                headless=self._headless,
                **visualizer_args,
                )

        self.add_flow(receiver_operator, hdmi_converter_operator, {("output", "input")})
        self.add_flow(hdmi_converter_operator, visualizer, {("output", "receivers")})

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
        os.path.dirname(__file__), "hdmi_configuration.yaml"
    )
    parser.add_argument(
        "--configuration",
        default=default_configuration,
        help="Configuration file",
    )
    parser.add_argument(
        "--hololink",
        default="192.168.0.2",
        help="IP address of Hololink board",
    )
    parser.add_argument(
        "--convert-3d",
        type=int,
        default=0,
        help="Convert Line By Line to Side By Side",
    )
    parser.add_argument(
        "--log-level",
        type=int,
        default=20,
        help="Logging level to display",
    )

    parser.add_argument(
        "--pattern",
        type=int,
        choices=range(12),
        help="Configure to display a test pattern.",
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
    # Get a handle to the data source
    channel_metadata = hololink_module.Enumerator.find_channel(channel_ip=args.hololink)
    logging.info(f"{channel_metadata=}")
    hololink_channel = hololink_module.DataChannel(channel_metadata)

    # Run it.
    hololink = hololink_channel.hololink()
    hololink.start()
    hololink.reset()
    # Get a handle to the camera
    camera = hololink_module.sensors.HDMISource(hololink_channel, 0, args.disable_i2c)
    if args.video_4k:
        logging.info("Force to 4k")
        hdmi._width = 3840
        hdmi._height = 2160

    # Set up the application
    application = HoloscanApplication(
        args.headless,
        args.fullscreen,
        args.convert_3d,
        cu_context,
        cu_device_ordinal,
        hololink_channel,
        camera,
        args.frame_limit,
    )
    application.config(args.configuration)
    logging.info("Calling run")
    application.run()
    hololink.stop()

    (cu_result,) = cuda.cuDevicePrimaryCtxRelease(cu_device)
    assert cu_result == cuda.CUresult.CUDA_SUCCESS


if __name__ == "__main__":
    main()
