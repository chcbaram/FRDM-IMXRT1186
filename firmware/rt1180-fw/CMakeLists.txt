cmake_minimum_required(VERSION 3.20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools")
include(arm-none-eabi-gcc)


# 프로젝트 이름은 최종 출력 파일 이름의 접두어로 사용된다.
#
set(PRJ_NAME rt1180-fw)


project(${PRJ_NAME}
  LANGUAGES ASM C CXX
)


# CM7 이미지를 함께 빌드한다. 켜면 CM33 이 부팅 중에 CM7 을 깨운다 (로드맵 25).
#
option(BUILD_CM7 "build Cortex-M7 image" OFF)


# 산출물 배치
#   build/cm33/   CM33 의 elf / bin / map
#   build/cm7/    CM7 의 elf / bin / map  (로드맵 25)
#   build/        통합 이미지 자리 (로드맵 44)


#-------------------------------------------------------------------------------
# 코어가 공유하는 것
#   - src/common    : 포터블 코어와 드라이버 공개 헤더
#   - src/lib/cmsis : CMSIS 6 Core
#   - src/lib/nxp   : MCUXpresso SDK 에서 골라 온 파일 (tools/fetch_nxp_sdk.py)
#-------------------------------------------------------------------------------
set(SHARED_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(NXP_DIR       ${SHARED_DIR}/lib/nxp)
set(NXP_DEV_DIR   ${NXP_DIR}/devices/MIMXRT1186)

file(GLOB_RECURSE SHARED_SRC_FILES CONFIGURE_DEPENDS
  ${SHARED_DIR}/common/*.c
  )

set(SHARED_INC_DIRS
  ${SHARED_DIR}
  ${SHARED_DIR}/common
  ${SHARED_DIR}/common/core
  ${SHARED_DIR}/common/hw/include
  ${SHARED_DIR}/cpu/shared      # 두 코어가 합의해야 하는 규약 (shared.h)
  ${SHARED_DIR}/lib

  ${SHARED_DIR}/lib/cmsis/Core/Include
  ${NXP_DEV_DIR}
  ${NXP_DIR}/devices/periph
  ${NXP_DIR}/drivers
  ${NXP_DIR}/boards/frdmimxrt1186
  )

# SDK 드라이버는 glob 하지 않고 쓰는 것만 명시한다.
#   전체를 받아 오지도 않았지만, 받아 둔 것이라도 쓰지 않는 드라이버를 컴파일하면
#   의존 헤더가 없어서 깨진다. 드라이버를 추가할 때 여기와 fetch_nxp_sdk.py 를 같이 고친다.
#
set(NXP_SRC_FILES
  ${NXP_DIR}/drivers/fsl_common.c
  ${NXP_DIR}/drivers/fsl_common_arm.c
  ${NXP_DIR}/drivers/fsl_clock.c
  ${NXP_DIR}/drivers/fsl_pmu.c
  ${NXP_DIR}/drivers/fsl_dcdc.c
  ${NXP_DIR}/drivers/fsl_rgpio.c
  ${NXP_DIR}/drivers/fsl_lpuart.c
  )


# Python 은 코어별 compile_commands 분리와 CM7 이미지 헤더(tools/mkcm7img.py)에 쓴다.
#
find_package(Python3 COMPONENTS Interpreter)

if(BUILD_CM7 AND NOT Python3_FOUND)
  message(FATAL_ERROR "BUILD_CM7=ON 에는 python3 가 필요하다 (tools/mkcm7img.py)")
endif()


add_subdirectory(src/cpu/cm33)

if(BUILD_CM7)
  add_subdirectory(src/cpu/cm7)
endif()


# 두 코어가 같은 SDK 소스를 서로 다른 -mcpu 로 컴파일한다. 통합 compile_commands.json
# 에는 공유 파일이 항목 두 개로 들어가고 IntelliSense 는 먼저 찾은 것을 쓰므로,
# 코어별로 갈라 놓아야 워크스페이스가 자기 코어 기준으로 해석한다. (titan-mini 와 같다)
#
if(Python3_FOUND)
  add_custom_target(compdb ALL
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/split_compdb.py
            ${CMAKE_BINARY_DIR}
    COMMENT "코어별 compile_commands.json 생성"
    VERBATIM
    )
else()
  message(STATUS "Python3 없음 - 코어별 compile_commands.json 을 만들지 않는다")
endif()


include(flash)
