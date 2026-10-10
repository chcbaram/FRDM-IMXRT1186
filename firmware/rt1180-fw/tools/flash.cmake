#-------------------------------------------------------------------------------
# probe-rs 기록을 CMake 타겟으로 만든다. 셸 스크립트를 두지 않는 이유는
# Windows / Linux / macOS 에서 같은 명령으로 돌리기 위해서다.
#
#   cmake --build build --target flash
#
# probe-rs 는 MIMXRT1180 타깃과 FlexSPI2 QSPI 기록 알고리즘을 내장하고 있어서
# 팩 파일이나 제조사 도구가 필요 없다. 온보드 MCU-Link(CMSIS-DAP)를 그대로 쓴다.
# elf 의 로드 주소(0x0400_0400 FCB, 0x0400_1000 컨테이너, 0x0400_B000 이미지)를
# 보고 필요한 섹터만 지우고 쓴다.
#
# 쓰고 나면 리셋한다. probe-rs 의 리셋은 SWD 로 SYSRESETREQ 를 쓰는 소프트 리셋이다
# (SRSR = CM33_REQUEST, docs/24-rtc-reset.md). BootROM 부터 다시 부팅하지만 POR 이
# 아니므로 BOOT_MODE 딥스위치를 다시 샘플하지는 않는다.
#-------------------------------------------------------------------------------
find_program(PROBE_RS_EXECUTABLE NAMES probe-rs probe-rs.exe)

set(PROBE_RS_CHIP "MIMXRT1180" CACHE STRING "probe-rs chip name")

if(PROBE_RS_EXECUTABLE)
  set(_flash_elfs $<TARGET_FILE:${PRJ_NAME}-cm33.elf>)
  set(_flash_deps ${PRJ_NAME}-cm33.elf)

  #-- CM7 이미지는 elf 가 아니라 헤더를 붙인 bin(.img)을 CM7 슬롯에 쓴다.
  #   CM7 elf 의 주소는 CM7 ITCM(0x0)이라 그대로는 플래시에 쓸 수 없다.
  #
  set(_flash_cm7_cmds "")
  if(BUILD_CM7)
    list(APPEND _flash_deps ${PRJ_NAME}-cm7.elf)
    set(_flash_cm7_cmds
      COMMAND ${PROBE_RS_EXECUTABLE} download
              --chip ${PROBE_RS_CHIP}
              --verify
              --binary-format bin
              --base-address 0x04800000
              ${CMAKE_BINARY_DIR}/cm7/${PRJ_NAME}-cm7.img
      )
  endif()

  add_custom_target(flash
    ${_flash_cm7_cmds}
    COMMAND ${PROBE_RS_EXECUTABLE} download
            --chip ${PROBE_RS_CHIP}
            --verify
            ${_flash_elfs}
    COMMAND ${PROBE_RS_EXECUTABLE} reset
            --chip ${PROBE_RS_CHIP}
    DEPENDS ${_flash_deps}
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
