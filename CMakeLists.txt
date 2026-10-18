cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3_vampire_keyboard)

# The reference Logitech K120 capture uses bMaxPacketSize0 = 8.  Override
# TinyUSB's default EP0 size (64) before components are compiled, and set
# the same value in usb_device_descriptor.bMaxPacketSize0.
idf_build_set_property(COMPILE_DEFINITIONS "CFG_TUD_ENDPOINT0_SIZE=8" APPEND)
