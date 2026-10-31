#-------------------------------------------------------------------------------
# CM33 : 부트 코어 (Secure 상태로 실행)
#-------------------------------------------------------------------------------
set(CORE_NAME  cm33)
set(EXECUTABLE ${PRJ_NAME}-${CORE_NAME}.elf)

set(CORE_DIR   ${CMAKE_CURRENT_SOURCE_DIR})
set(CORE_OUT   ${CMAKE_BINARY_DIR}/${CORE_NAME})


# 이 코어의 소스
#
file(GLOB SRC_FILES CONFIGURE_DEPENDS
  ${CORE_DIR}/*.c
  ${CORE_DIR}/*.cpp
  )

file(GLOB_RECURSE SRC_FILES_RECURSE CONFIGURE_DEPENDS
  ${CORE_DIR}/ap/*.c
  ${CORE_DIR}/ap/*.cpp
  ${CORE_DIR}/bsp/*.c
  ${CORE_DIR}/bsp/*.S
  ${CORE_DIR}/hw/*.c
  )

# 디바이스 파일 : startup 과 SystemInit 은 SDK 원본을 그대로 쓴다.
#
set(NXP_CORE_FILES
  ${NXP_DEV_DIR}/gcc/startup_MIMXRT1186_cm33.S
  ${NXP_DEV_DIR}/system_MIMXRT1186_cm33.c
  )

# FreeRTOS 포트 : TrustZone 없이 Secure 로만 돈다 (bsp/rtos/FreeRTOSConfig.h)
#
set(FREERTOS_PORT_DIR ${FREERTOS_DIR}/portable/GCC/ARM_CM33_NTZ/non_secure)
set(FREERTOS_CORE_FILES
  ${FREERTOS_SRC_FILES}
  ${FREERTOS_PORT_DIR}/port.c
  ${FREERTOS_PORT_DIR}/portasm.c
  )


add_executable(${EXECUTABLE}
  ${SRC_FILES}
  ${SRC_FILES_RECURSE}
  ${SHARED_SRC_FILES}
  ${NXP_SRC_FILES}
  ${NXP_CORE_FILES}
  ${FREERTOS_CORE_FILES}
  )


# 같이 빌드한 CM7 이미지의 CRC 를 앱에 심는다 (tools/mkcm7pair.py, docs/44).
#   부팅 때 QSPI CM7 슬롯과 비교해 다르면 경고한다. CM7 은 그대로 깨운다.
#
set(CM7_PAIR_SRC ${CORE_OUT}/cm7_pair.c)
if(BUILD_CM7)
  set(CM7_IMG ${CMAKE_BINARY_DIR}/cm7/${PRJ_NAME}-cm7.img)
  add_custom_command(OUTPUT ${CM7_PAIR_SRC}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mkcm7pair.py ${CM7_IMG} ${CM7_PAIR_SRC}
    DEPENDS ${CM7_IMG} ${CMAKE_SOURCE_DIR}/tools/mkcm7pair.py
    COMMENT "CM7 짝 CRC"
    )
else()
  file(CONFIGURE OUTPUT ${CM7_PAIR_SRC} CONTENT
"// tools/mkcm7pair.py 와 같은 형식. BUILD_CM7=OFF\n#include <stdint.h>\n\nconst uint32_t ipc_cm7_pair_crc = 0x00000000UL;\n")
endif()
target_sources(${EXECUTABLE} PRIVATE ${CM7_PAIR_SRC})


# elf / bin / map 을 build/<코어>/ 로 모은다
#
set_target_properties(${EXECUTABLE} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CORE_OUT}
  )


target_include_directories(${EXECUTABLE} PRIVATE
  ${CORE_DIR}
  ${CORE_DIR}/ap
  ${CORE_DIR}/ap/modules
  ${CORE_DIR}/ap/modules/common/cmd
  ${CORE_DIR}/bsp
  ${CORE_DIR}/bsp/rtos
  ${CORE_DIR}/hw
  ${CORE_DIR}/hw/driver

  ${SHARED_INC_DIRS}
  ${FREERTOS_DIR}/include
  ${FREERTOS_PORT_DIR}
  )

target_compile_definitions(${EXECUTABLE} PRIVATE
  CPU_MIMXRT1186CVJ8C_cm33        # fsl_device_registers.h 가 디바이스 헤더를 고른다

  #-- CM7 이미지를 함께 빌드했을 때만 CM33 이 CM7 을 깨운다 (titan-mini 와 같다)
  _HW_DEF_CM7_IMAGE=$<BOOL:${BUILD_CM7}>

  #-- startup_MIMXRT1186_cm33.S 옵션
  __STARTUP_CLEAR_BSS
  __STARTUP_INITIALIZE_RAMFUNCTION
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

# SDK 소스는 프로젝트 코드와 경고 수준을 따로 간다.
#
set_source_files_properties(
  ${NXP_SRC_FILES}
  ${NXP_CORE_FILES}
  ${FREERTOS_CORE_FILES}
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


# .elf -> .bin -> -tag.bin
#   bin 은 0x0404_1000(벡터) 부터 시작한다. objcopy 는 가장 낮은 로드 주소부터 잘라 내기 때문이다.
#   -tag.bin 은 앞에 TAG 4 KB 를 붙인 것으로 0x0404_0000 에 쓴다 (tools/mkfwtag.py, docs/42).
#
add_custom_command(TARGET ${EXECUTABLE}
  POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} ARGS -O binary
          $<TARGET_FILE:${EXECUTABLE}>
          ${CORE_OUT}/${PRJ_NAME}-${CORE_NAME}.bin
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mkfwtag.py
          ${CORE_OUT}/${PRJ_NAME}-${CORE_NAME}.bin
  COMMENT "Invoking: Make Bin + TAG"
  )
