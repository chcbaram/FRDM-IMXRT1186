#-------------------------------------------------------------------------------
# CM7 : 792 MHz 코어 (CM33 이 깨운다)
#
# 켜는 법: cmake -S . -B build -G Ninja -DBUILD_CM7=ON
#
# 결과물
#   build/cm7/rt1180-fw-cm7.elf   ITCM 0x0 에 링크된 이미지
#   build/cm7/rt1180-fw-cm7.bin   ITCM 한 덩어리
#   build/cm7/rt1180-fw-cm7.img   bin 앞에 헤더(magic, 크기, CRC)를 붙인 것 → QSPI 0x0480_0000
#-------------------------------------------------------------------------------
set(CORE_NAME  cm7)
set(EXECUTABLE ${PRJ_NAME}-${CORE_NAME}.elf)

set(CORE_DIR   ${CMAKE_CURRENT_SOURCE_DIR})
set(CORE_OUT   ${CMAKE_BINARY_DIR}/${CORE_NAME})


file(GLOB SRC_FILES CONFIGURE_DEPENDS
  ${CORE_DIR}/*.c
  )

file(GLOB_RECURSE SRC_FILES_RECURSE CONFIGURE_DEPENDS
  ${CORE_DIR}/ap/*.c
  ${CORE_DIR}/bsp/*.c
  ${CORE_DIR}/hw/*.c
  )

set(NXP_CORE_FILES
  ${NXP_DEV_DIR}/gcc/startup_MIMXRT1186_cm7.S
  ${NXP_DEV_DIR}/system_MIMXRT1186_cm7.c
  )

# CM7 은 아직 기동 확인만 한다. 공용 드라이버(common)도 필요할 때 더한다.
#
add_executable(${EXECUTABLE}
  ${SRC_FILES}
  ${SRC_FILES_RECURSE}
  ${NXP_SRC_FILES}
  ${NXP_CORE_FILES}
  )

set_target_properties(${EXECUTABLE} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CORE_OUT}
  )

target_include_directories(${EXECUTABLE} PRIVATE
  ${CORE_DIR}
  ${CORE_DIR}/ap
  ${CORE_DIR}/bsp
  ${CORE_DIR}/hw
  ${CORE_DIR}/hw/driver

  ${SHARED_INC_DIRS}
  )

target_compile_definitions(${EXECUTABLE} PRIVATE
  CPU_MIMXRT1186CVJ8C_cm7
  __STARTUP_CLEAR_BSS
  )

set(CORE_CPU_FLAGS
  -mcpu=cortex-m7
  -mthumb
  -mfloat-abi=hard
  -mfpu=fpv5-d16                  # CM7 은 배정도 FPU
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
  TARGET_DIRECTORY ${EXECUTABLE}
  PROPERTIES
    COMPILE_OPTIONS "-Os;-w"
  )

target_link_options(${EXECUTABLE} PRIVATE
  -T${CORE_DIR}/bsp/ldscript/${PRJ_NAME}-${CORE_NAME}.ld

  ${CORE_CPU_FLAGS}
  -specs=nano.specs
  -specs=nosys.specs

  -lc
  -lm

  -Wl,-Map=${CORE_OUT}/${PRJ_NAME}-${CORE_NAME}.map,--cref
  -Wl,--gc-sections
  -Wl,--no-warn-rwx-segments
  -Xlinker -print-memory-usage -Xlinker
  )


# .elf -> .bin -> .img (헤더 추가)
#   bin 은 ITCM 0x0 부터 .data 초기값까지 한 덩어리다. NOLOAD 인 .shared 는 빠진다.
#
add_custom_command(TARGET ${EXECUTABLE}
  POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} ARGS -O binary
          -j .interrupts -j .text -j .ARM.extab -j .ARM -j .preinit_array
          -j .init_array -j .fini_array -j .data
          $<TARGET_FILE:${EXECUTABLE}>
          ${CORE_OUT}/${PRJ_NAME}-${CORE_NAME}.bin
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mkcm7img.py
          ${CORE_OUT}/${PRJ_NAME}-${CORE_NAME}.bin
          ${CORE_OUT}/${PRJ_NAME}-${CORE_NAME}.img
  COMMENT "Invoking: Make CM7 bin / img"
  )
