cmake_minimum_required(VERSION 3.13)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../tools")
include(arm-none-eabi-gcc)

set(PRJ_NAME h5-fw)

project(${PRJ_NAME}
  LANGUAGES ASM C
)

set(EXECUTABLE ${PRJ_NAME}.elf)

set(FW_ROOT  ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(COMMON   ${FW_ROOT}/common)
set(LIB      ${FW_ROOT}/lib)
set(HAL_SRC  ${LIB}/STM32H5xx_HAL_Driver/Src)

# TAG 를 붙이는 도구는 PC 의 Python 으로 실행한다
find_package(Python3 REQUIRED COMPONENTS Interpreter)


file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
  src/*.c
  src/*.s
  ${COMMON}/core/*.c
  ${COMMON}/hw/src/*.c        # boot / fw 가 함께 쓰는 드라이버 (flash.c)
)

set(HAL_FILES
  ${HAL_SRC}/stm32h5xx_hal.c
  ${HAL_SRC}/stm32h5xx_hal_cortex.c
  ${HAL_SRC}/stm32h5xx_hal_rcc.c
  ${HAL_SRC}/stm32h5xx_hal_rcc_ex.c
  ${HAL_SRC}/stm32h5xx_hal_gpio.c
  ${HAL_SRC}/stm32h5xx_hal_exti.c
  ${HAL_SRC}/stm32h5xx_hal_pwr.c
  ${HAL_SRC}/stm32h5xx_hal_pwr_ex.c
  ${HAL_SRC}/stm32h5xx_hal_flash.c
  ${HAL_SRC}/stm32h5xx_hal_flash_ex.c
  ${HAL_SRC}/stm32h5xx_hal_icache.c
  ${HAL_SRC}/stm32h5xx_hal_uart.c
  ${HAL_SRC}/stm32h5xx_hal_uart_ex.c
  ${HAL_SRC}/stm32h5xx_hal_xspi.c       # W6300 QSPI
  ${HAL_SRC}/stm32h5xx_ll_dlyb.c        # xspi 가 함께 필요로 함
  ${HAL_SRC}/stm32h5xx_hal_dma.c        # xspi 헤더가 DMA 타입을 쓰므로 필요 (지금은 DMA 안 씀)
  ${HAL_SRC}/stm32h5xx_hal_dma_ex.c
  ${HAL_SRC}/stm32h5xx_hal_fdcan.c      # CAN-FD
)

# WIZnet ioLibrary (W6300): 칩 설정, 소켓, DHCP
set(WIZ      ${LIB}/wiznet)
set(WIZ_FILES
  ${WIZ}/wizchip_conf.c
  ${WIZ}/socket/socket.c
  ${WIZ}/dhcp/dhcp.c
)

# FreeRTOS: 커널 + Cortex-M33(TrustZone 없음) 포트 + heap_4
set(RTOS     ${LIB}/FreeRTOS)
set(RTOS_PORT ${RTOS}/portable/GCC/ARM_CM33_NTZ/non_secure)
set(RTOS_FILES
  ${RTOS}/tasks.c
  ${RTOS}/list.c
  ${RTOS}/queue.c
  ${RTOS}/timers.c
  ${RTOS}/event_groups.c
  ${RTOS}/stream_buffer.c
  ${RTOS_PORT}/port.c
  ${RTOS_PORT}/portasm.c
  ${RTOS}/portable/MemMang/heap_4.c
)

add_executable(${EXECUTABLE}
  ${SRC_FILES}
  ${HAL_FILES}
  ${RTOS_FILES}
  ${WIZ_FILES}
)

target_include_directories(${EXECUTABLE} PRIVATE
  src
  src/ap
  src/bsp
  src/bsp/device
  src/bsp/rtos
  src/hw

  ${COMMON}
  ${COMMON}/core
  ${COMMON}/hw/include

  ${LIB}/CMSIS/Core/Include
  ${LIB}/CMSIS/Device/ST/STM32H5xx/Include
  ${LIB}/STM32H5xx_HAL_Driver/Inc
  ${RTOS}/include
  ${RTOS_PORT}
  ${LIB}                        # "wiznet/wizchip_conf.h" 처럼 폴더 이름부터 쓰는 include 를 위해
)

target_compile_definitions(${EXECUTABLE} PRIVATE
  STM32H563xx
  USE_HAL_DRIVER
  _WIZCHIP_QSPI_MODE_=QSPI_QUAD_MODE    # W6300 을 Quad(4줄)로 쓴다
  DHCP_WAIT_TIME=3                      # DHCP 대답이 없으면 3초 뒤 다시 묻기 (원래 10초)
)

set(CPU_FLAGS
  -mcpu=cortex-m33
  -mthumb
  -mfpu=fpv5-sp-d16
  -mfloat-abi=hard
)

target_compile_options(${EXECUTABLE} PRIVATE
  ${CPU_FLAGS}
  -fdata-sections
  -ffunction-sections
  -Wall
  -g3
  -Og
)

target_link_options(${EXECUTABLE} PRIVATE
  -T${CMAKE_CURRENT_SOURCE_DIR}/src/bsp/ldscript/h5-fw.ld
  ${CPU_FLAGS}
  --specs=nano.specs
  --specs=nosys.specs
  -Wl,-Map=${PRJ_NAME}.map,--cref
  -Wl,--gc-sections
  -Wl,--print-memory-usage
)

if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 12)
  target_link_options(${EXECUTABLE} PRIVATE -Wl,--no-warn-rwx-segments)
endif()

# 빌드 결과물
#   h5-fw.bin      : 앱 이미지만 (0x08010400 부터). 네트워크 업데이트 때 GUI 에 넣는 파일
#   h5-fw_tag.bin  : TAG + 앱 이미지 (0x08010000 부터). SWD 로 직접 구울 때 쓰는 파일
add_custom_command(TARGET ${EXECUTABLE} POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary ${EXECUTABLE} ${PRJ_NAME}.bin
  COMMAND ${CMAKE_OBJCOPY} -O ihex   ${EXECUTABLE} ${PRJ_NAME}.hex
  COMMAND ${Python3_EXECUTABLE} ${FW_ROOT}/tools/fw_tag.py ${PRJ_NAME}.bin ${PRJ_NAME}_tag.bin --name H5-ETH-FDCAN-FW
  COMMENT "Generating ${PRJ_NAME}.bin / .hex / _tag.bin"
)
