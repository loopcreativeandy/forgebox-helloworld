set(CMAKE_SYSTEM_NAME Generic)
cmake_minimum_required(VERSION 3.10)

set(BUILD_ENVIRONMENT "DEBUG")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g2 -ggdb")
set(CMAKE_C_FLAGS_RELEASE "-O3")

# Must include firmware.cmake before project() to set up compiler
include(firmware.cmake)

project(mh1903)

set(COMMON_DEFINITIONS
    USE_STDPERIPH_DRIVER
    USE_FULL_ASSERT
    ARM_MATH_CM4
    __TARGET_FPU_VFP
    NDEBUG
    CONFIG_USB_DEVICE_MSC=1
    CONFIG_MHSCPU_MH1903=1
)

# Include paths
set(EXTERNAL_INCLUDE_PATH
    external
    external/FreeRTOS
    external/FreeRTOS/Source/include
    external/FreeRTOS/Source/portable/GCC/ARM_CM4F
    external/FreeRTOS/CMSIS_RTOS_V2
    external/lvgl
    external/lvgl/src
    external/lvgl/src/core
    external/lvgl/src/draw
    external/lvgl/src/extra
    external/lvgl/src/font
    external/lvgl/src/hal
    external/lvgl/src/misc
    external/lvgl/src/widgets
    external/mh1903_lib/MHSCPU_Driver/inc
    external/mh1903_lib/Device/MegaHunt/mhscpu/Include
    external/mh1903_lib/CMSIS/Include
    external/mh1903_lib/SCPU_USB_Lib/SCPU_USB_OTG_Driver/inc
    external/mh1903_lib/SCPU_USB_Lib/SCPU_USB_Device_Library/Core/inc
    external/mh1903_lib/SCPU_USB_Lib/SCPU_USB_Device_Library/Class/cdc/inc
    external/monocypher
)

set(SRC_INCLUDE_PATH
    src
    src/config
    src/tasks
    src/driver
    src/ram
    src/utils
    src/driver/usb
    src/usb
    src/wallet
)

set(INCLUDE_DIR
    ${SRC_INCLUDE_PATH}
    ${EXTERNAL_INCLUDE_PATH}
)

# LVGL source files
file(GLOB_RECURSE LVGL
    "external/lvgl/src/core/*.c"
    "external/lvgl/src/draw/*.c"
    "external/lvgl/src/hal/*.c"
    "external/lvgl/src/misc/*.c"
    "external/lvgl/src/widgets/*.c"
    "external/lvgl/src/extra/*.c"
    "external/lvgl/src/font/*.c"
)

# FreeRTOS source files
file(GLOB_RECURSE FREERTOS
    "external/FreeRTOS/Source/croutine.c"
    "external/FreeRTOS/Source/event_groups.c"
    "external/FreeRTOS/Source/list.c"
    "external/FreeRTOS/Source/queue.c"
    "external/FreeRTOS/Source/stream_buffer.c"
    "external/FreeRTOS/Source/tasks.c"
    "external/FreeRTOS/Source/timers.c"
    "external/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c"
    "external/FreeRTOS/CMSIS_RTOS_V2/cmsis_os2.c"
    "external/FreeRTOS/Source/portable/MemMang/heap_4.c"
    "external/FreeRTOS/rtos_expand.c"
)

# USB device stack (Keystone's patched copy; device mode + cdc/WinUSB class only)
set(USB_LIB
    external/mh1903_lib/SCPU_USB_Lib/SCPU_USB_OTG_Driver/src/usb_core.c
    external/mh1903_lib/SCPU_USB_Lib/SCPU_USB_OTG_Driver/src/usb_dcd.c
    external/mh1903_lib/SCPU_USB_Lib/SCPU_USB_OTG_Driver/src/usb_dcd_int.c
    external/mh1903_lib/SCPU_USB_Lib/SCPU_USB_Device_Library/Core/src/usbd_core.c
    external/mh1903_lib/SCPU_USB_Lib/SCPU_USB_Device_Library/Core/src/usbd_req.c
    external/mh1903_lib/SCPU_USB_Lib/SCPU_USB_Device_Library/Core/src/usbd_ioreq.c
    external/mh1903_lib/SCPU_USB_Lib/SCPU_USB_Device_Library/Class/cdc/src/usbd_cdc_core.c
)

# MH1903 driver source files
aux_source_directory(external/mh1903_lib/MHSCPU_Driver/src/ MH1903_DRIVER)

set(APP_SRC
    src/main.c
    src/tasks/helloworld_task.c
    src/driver/drv_sys.c
    src/driver/drv_uart.c
    src/driver/drv_psram.c
    src/driver/drv_lcd_bright.c
    src/driver/drv_parallel8080.c
    src/driver/drv_ili9806.c
    src/driver/drv_nt35510.c
    src/driver/hal_lcd.c
    src/driver/drv_aw32001.c
    src/driver/drv_battery.c
    src/driver/drv_i2c_io.c
    src/ram/psram_heap_4.c
    src/ram/user_memory.c
    src/utils/assert.c
    src/utils/user_delay.c
    src/utils/user_utils.c
    src/config/version.c
    src/driver/drv_power.c
    src/driver/low_power.c
    src/ui/imgDevLogo.c
    src/ui/openSansEnTitle.c
    src/ui/openSansEnText.c
    src/driver/usb/drv_usb.c
    src/driver/usb/usb_bsp.c
    src/driver/usb/usbd_cdc_vcp.c
    src/driver/usb/usbd_composite.c
    src/driver/usb/usbd_desc.c
    src/driver/usb/usbd_usr.c
    src/usb/usb_task.c
    src/usb/eapdu.c
    src/usb/eapdu_framing.c
    src/wallet/sol_key.c
    external/monocypher/monocypher.c
    external/monocypher/monocypher-ed25519.c
)

set(objsrc 
    ${MH1903_DRIVER}
    ${APP_SRC}
    ${USB_LIB}
    ${FREERTOS}
    ${LVGL}
    external/mh1903_lib/Device/MegaHunt/mhscpu/Source/GCC/startup_mhscpu.s
)

add_executable(${PROJECT_NAME}.elf ${objsrc})
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE ${COMMON_DEFINITIONS})
target_include_directories(${PROJECT_NAME}.elf PRIVATE ${INCLUDE_DIR})

target_link_libraries(${PROJECT_NAME}.elf m)

set(ELF_FILE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.elf)
set(HEX_FILE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.hex)
set(BIN_FILE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.bin)

add_custom_command(TARGET "${PROJECT_NAME}.elf" POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -Obinary ${ELF_FILE} ${BIN_FILE}
    COMMAND ${CMAKE_OBJCOPY} -Oihex ${ELF_FILE} ${HEX_FILE}
    COMMENT "Building ${PROJECT_NAME}.bin and ${PROJECT_NAME}.hex"

    COMMAND ${CMAKE_COMMAND} -E copy ${HEX_FILE} "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.hex"
    COMMAND ${CMAKE_COMMAND} -E copy ${BIN_FILE} "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin"

    COMMAND ${CMAKE_SIZE} --format=berkeley ${PROJECT_NAME}.elf ${PROJECT_NAME}.hex
    COMMENT "Building ${PROJECT_NAME}.bin and ${PROJECT_NAME}.hex; Invoking: Cross ARM GNU Print Size"
)