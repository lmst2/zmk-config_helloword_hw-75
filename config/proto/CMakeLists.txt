# Copyright (c) 2022-2023 XiNGRZ
# SPDX-License-Identifier: MIT

zephyr_library_named(hw75_proto)

if(CONFIG_HW75_PROTO)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ZEPHYR_NANOPB_MODULE_DIR}/extra)
find_package(Nanopb REQUIRED)

set(PROTO_DEFS)

if(CONFIG_HW75_PROTO_USB_COMM)
    list(APPEND PROTO_DEFS usb_comm.proto)

    # Per-board nanopb options: mark every oneof payload that the current
    # board never handles as FT_IGNORE, so the generated MessageH2D /
    # MessageD2H union drops it entirely. The .proto file is untouched, so
    # host tooling (protobufjs) still sees the full schema. We stage the
    # selected file as usb_comm.options under the build tree because
    # FindNanopb always looks for "<proto_name>.options" by filename.
    if(CONFIG_BOARD_HW75_KEYBOARD)
        set(_usb_comm_options_src ${CMAKE_CURRENT_SOURCE_DIR}/usb_comm.keyboard.options)
    elseif(CONFIG_BOARD_HW75_DYNAMIC)
        set(_usb_comm_options_src ${CMAKE_CURRENT_SOURCE_DIR}/usb_comm.dynamic.options)
    endif()

    if(DEFINED _usb_comm_options_src)
        set(_usb_comm_options_out ${CMAKE_CURRENT_BINARY_DIR}/nanopb_opts/usb_comm.options)
        configure_file(${_usb_comm_options_src} ${_usb_comm_options_out} COPYONLY)
        list(APPEND NANOPB_DEPENDS ${_usb_comm_options_out})
    endif()
endif()

if(CONFIG_HW75_PROTO_UART_COMM)
    list(APPEND PROTO_DEFS uart_comm.proto)
endif()

nanopb_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_DEFS})
zephyr_library_sources(${PROTO_SRCS} ${PROTO_HDRS})

zephyr_include_directories(${NANOPB_INCLUDE_DIRS})
zephyr_include_directories(${CMAKE_CURRENT_BINARY_DIR})

endif() # CONFIG_HW75_PROTO
