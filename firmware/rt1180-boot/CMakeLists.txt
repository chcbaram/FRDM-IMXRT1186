cmake_minimum_required(VERSION 3.20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../rt1180-fw/tools")
include(arm-none-eabi-gcc)


# 프로젝트 이름은 최종 출력 파일 이름으로 사용된다.
#
set(PRJ_NAME rt1180-boot)


project(${PRJ_NAME}
  LANGUAGES ASM C CXX
)


set(EXECUTABLE ${PRJ_NAME}.elf)


#-------------------------------------------------------------------------------
# 부트로더 : CM33 Code TCM 에서 실행 (non-XIP, docs/40 · 41)
#
# 앱(rt1180-fw)과 같은 드라이버를 쓰므로 소스를 복사하지 않고 경로로 가져온다.
#   - 이 폴더 : main · ap · bsp(bsp.c, 부트 헤더, 링커) · hw(hw.c, hw_def.h)
#   - 앱 폴더 : 공통 헤더 · NXP SDK · CM33 드라이버 일부 · clock.c · syscalls.c
#
# 인클루드는 이 폴더를 먼저 둔다. 그래서 드라이버가 부르는 hw_def.h · bsp.h 는
# 부트로더 것이 잡힌다.
#-------------------------------------------------------------------------------
set(FW_DIR       ${CMAKE_CURRENT_SOURCE_DIR}/../rt1180-fw/src)
set(FW_CORE_DIR  ${FW_DIR}/cpu/cm33)
set(NXP_DIR      ${FW_DIR}/lib/nxp)
set(NXP_DEV_DIR  ${NXP_DIR}/devices/MIMXRT1186)


file(GLOB SRC_FILES CONFIGURE_DEPENDS
  src/*.c
  src/*.cpp
  )

file(GLOB_RECURSE SRC_FILES_RECURSE CONFIGURE_DEPENDS
  src/ap/*.c
  src/ap/*.cpp
  src/bsp/*.c
  src/hw/*.c
  )

# 앱과 같이 쓰는 CM33 소스. 드라이버를 더할 때 hw_def.h 의 _USE_HW_* 와 짝을 맞춘다.
#
set(FW_SRC_FILES
  ${FW_CORE_DIR}/bsp/clock.c
  ${FW_CORE_DIR}/bsp/syscalls.c

  ${FW_CORE_DIR}/hw/driver/led.c
  ${FW_CORE_DIR}/hw/driver/uart.c
  ${FW_CORE_DIR}/hw/driver/cli.c
  ${FW_CORE_DIR}/hw/driver/log.c
  ${FW_CORE_DIR}/hw/driver/rtc.c
  ${FW_CORE_DIR}/hw/driver/reset.c
  ${FW_CORE_DIR}/hw/driver/qspi.c
  ${FW_CORE_DIR}/hw/driver/hw_info.c

  ${FW_CORE_DIR}/ap/modules/module.c
  )

file(GLOB_RECURSE FW_CORE_SRC_FILES CONFIGURE_DEPENDS
  ${FW_DIR}/common/core/*.c
  )

set(NXP_SRC_FILES
  ${NXP_DIR}/drivers/fsl_common.c
  ${NXP_DIR}/drivers/fsl_common_arm.c
  ${NXP_DIR}/drivers/fsl_clock.c
  ${NXP_DIR}/drivers/fsl_pmu.c
  ${NXP_DIR}/drivers/fsl_dcdc.c
  ${NXP_DIR}/drivers/fsl_rgpio.c
  ${NXP_DIR}/drivers/fsl_lpuart.c
  ${NXP_DIR}/drivers/fsl_romapi.c
  )

set(NXP_CORE_FILES
  ${NXP_DEV_DIR}/gcc/startup_MIMXRT1186_cm33.S
  ${NXP_DEV_DIR}/system_MIMXRT1186_cm33.c
  )


add_executable(${EXECUTABLE}
  ${SRC_FILES}
  ${SRC_FILES_RECURSE}
  ${FW_SRC_FILES}
  ${FW_CORE_SRC_FILES}
  ${NXP_SRC_FILES}
  ${NXP_CORE_FILES}
  )


target_include_directories(${EXECUTABLE} PRIVATE
  src
  src/ap
  src/ap/modules
  src/bsp
  src/hw

  ${FW_CORE_DIR}/ap/modules
  ${FW_CORE_DIR}/bsp
  ${FW_CORE_DIR}/hw/driver

  ${FW_DIR}
  ${FW_DIR}/common
  ${FW_DIR}/common/core
  ${FW_DIR}/common/hw/include
  ${FW_DIR}/lib

  ${FW_DIR}/lib/cmsis/Core/Include
  ${NXP_DEV_DIR}
  ${NXP_DIR}/devices/periph
  ${NXP_DIR}/drivers
  ${NXP_DIR}/boards/frdmimxrt1186
  )

target_compile_definitions(${EXECUTABLE} PRIVATE
  CPU_MIMXRT1186CVJ8C_cm33        # fsl_device_registers.h 가 디바이스 헤더를 고른다

  #-- startup_MIMXRT1186_cm33.S 옵션
  #   __STARTUP_INITIALIZE_RAMFUNCTION 은 두지 않는다. 코드 전체가 이미 Code TCM 에 있다.
  __STARTUP_CLEAR_BSS
  )

set(CORE_CPU_FLAGS
  -mcpu=cortex-m33
  -mthumb
  -mfloat-abi=hard
  -mfpu=fpv5-sp-d16
  -mcmse                          # Secure 로 빌드 → 레지스터 베이스가 0x5xxx_xxxx (docs/02 2절)
  )

target_compile_options(${EXECUTABLE} PRIVATE
  ${CORE_CPU_FLAGS}

  -fdata-sections
  -ffunction-sections

  -fstack-usage
  -Wmissing-field-initializers
  -Wuninitialized

  -Wall
  -g3
  -Og
  )

set_source_files_properties(
  ${NXP_SRC_FILES}
  ${NXP_CORE_FILES}
  PROPERTIES
    COMPILE_OPTIONS "-Os;-w"
  )

target_link_options(${EXECUTABLE} PRIVATE
  -T${CMAKE_CURRENT_SOURCE_DIR}/src/bsp/ldscript/${PRJ_NAME}.ld

  ${CORE_CPU_FLAGS}
  -specs=nano.specs
  -specs=nosys.specs

  -lc
  -lm

  -Wl,-Map=${PRJ_NAME}.map,--cref
  -Wl,--gc-sections
  -Wl,--no-warn-rwx-segments
  -Xlinker -print-memory-usage -Xlinker
  )


# .elf -> .bin
#   objcopy 는 로드 주소(LMA)로 자른다. bin 은 0x0400_0400(FCB) 부터 부트 슬롯 끝까지다.
#
add_custom_command(TARGET ${EXECUTABLE}
  POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} ARGS -O binary
          $<TARGET_FILE:${EXECUTABLE}>
          ${CMAKE_BINARY_DIR}/${PRJ_NAME}.bin
  COMMENT "Invoking: Make Bin"
  )


#-------------------------------------------------------------------------------
# probe-rs 기록 : 부트 슬롯(0x0400_0000~)만 쓴다. elf 의 로드 주소를 보고 쓴다.
#
#   cmake --build build --target flash
#-------------------------------------------------------------------------------
find_program(PROBE_RS_EXECUTABLE NAMES probe-rs probe-rs.exe)

set(PROBE_RS_CHIP "MIMXRT1180" CACHE STRING "probe-rs chip name")

if(PROBE_RS_EXECUTABLE)
  add_custom_target(flash
    COMMAND ${PROBE_RS_EXECUTABLE} download
            --chip ${PROBE_RS_CHIP}
            --verify
            $<TARGET_FILE:${EXECUTABLE}>
    COMMAND ${PROBE_RS_EXECUTABLE} reset
            --chip ${PROBE_RS_CHIP}
    DEPENDS ${EXECUTABLE}
    USES_TERMINAL
    COMMENT "probe-rs download (${PROBE_RS_CHIP})"
    )
else()
  add_custom_target(flash
    COMMAND ${CMAKE_COMMAND} -E echo
            "flash 타겟에는 probe-rs 가 필요하다. firmware/docs/13-os-setup.md 참고."
    COMMAND ${CMAKE_COMMAND} -E false
    )
endif()
