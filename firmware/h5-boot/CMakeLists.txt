cmake_minimum_required(VERSION 3.13)

# 컴파일러를 PC용 gcc 가 아니라 arm-none-eabi-gcc 로 바꾸는 설정 (project() 보다 먼저)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../tools")
include(arm-none-eabi-gcc)

set(PRJ_NAME h5-boot)

project(${PRJ_NAME}
  LANGUAGES ASM C
)

set(EXECUTABLE ${PRJ_NAME}.elf)

# 공용 폴더 위치
set(FW_ROOT  ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(COMMON   ${FW_ROOT}/common)
set(LIB      ${FW_ROOT}/lib)
set(HAL_SRC  ${LIB}/STM32H5xx_HAL_Driver/Src)


# 우리 코드: src 아래 모든 .c/.s + 공용 core
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
  src/*.c
  src/*.s
  ${COMMON}/core/*.c
  ${COMMON}/hw/src/*.c        # boot / fw 가 함께 쓰는 드라이버 (flash.c)
)

# ST HAL: 쓰는 모듈만 골라서 컴파일 (hal_conf.h 의 MODULE_ENABLED 와 짝을 맞춘다)
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
  ${HAL_SRC}/stm32h5xx_hal_uart.c
  ${HAL_SRC}/stm32h5xx_hal_uart_ex.c
)

add_executable(${EXECUTABLE}
  ${SRC_FILES}
  ${HAL_FILES}
)

target_include_directories(${EXECUTABLE} PRIVATE
  src
  src/ap
  src/bsp
  src/bsp/device
  src/hw

  ${COMMON}
  ${COMMON}/core
  ${COMMON}/hw/include

  ${LIB}/CMSIS/Core/Include
  ${LIB}/CMSIS/Device/ST/STM32H5xx/Include
  ${LIB}/STM32H5xx_HAL_Driver/Inc
)

target_compile_definitions(${EXECUTABLE} PRIVATE
  STM32H563xx
  USE_HAL_DRIVER
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
  -T${CMAKE_CURRENT_SOURCE_DIR}/src/bsp/ldscript/h5-boot.ld
  ${CPU_FLAGS}
  --specs=nano.specs
  --specs=nosys.specs
  -Wl,-Map=${PRJ_NAME}.map,--cref
  -Wl,--gc-sections
  -Wl,--print-memory-usage
)

# GCC 12 이상(binutils 2.39+)에서만 생기는 경고를 끈다. GCC 10 에는 이 옵션이 없다.
if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 12)
  target_link_options(${EXECUTABLE} PRIVATE -Wl,--no-warn-rwx-segments)
endif()

# .elf -> .bin / .hex
add_custom_command(TARGET ${EXECUTABLE} POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary ${EXECUTABLE} ${PRJ_NAME}.bin
  COMMAND ${CMAKE_OBJCOPY} -O ihex   ${EXECUTABLE} ${PRJ_NAME}.hex
  COMMENT "Generating ${PRJ_NAME}.bin / .hex"
)
