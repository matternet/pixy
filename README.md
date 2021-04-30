libpixyusb API Reference:

http://charmedlabs.github.io/pixy/index.html

Pixy README

This directory contains:


/doc - this directory contains a doxygen configuration file for building doxygen documentation.

/scripts - this directory contains scripts for building pixy software modules.

/src/device - this directory contains code (firmware) that runs on the Pixy
(CMUcam5) device.

/src/host - this directory contains code that runs on the host computer.
(Windows PC, Linux PC, Mac)

/src/host/hello_pixy - this directory contains an example program that uses libpixyusb for communicating with Pixy.

/src/host/libpixyusb - this directory contains the USB library for communicating with Pixy.

/src/host/arduino - this directory contains the Arduino library for communicating with Pixy.


Firmware Build Procedure with GCC ARM Toolchain:

  Installation:

    Make sure LPCXpresso IDE v8.2.2 is installed
    https://www.nxp.com/design/microcontrollers-developer-resources/lpc-microcontroller-utilities/lpcxpresso-ide-v8-2-2:LPCXPRESSO

    Add the gcc arm toolchain that comes with LPCXpresso to your PATH before building.
    <LPCXPRESSO_ROOT>/lpcxpresso/tools/bin


  Build (without image logging):

    1) cd src/device
    2) make

  Build (with image logging):

    1) cd src/device
    2) make ENABLE_IMAGE_LOGGING=1

  Output:

    The firmware hex file is located in src/device/main_m4/SPIFI/pixy_firmware.hex
