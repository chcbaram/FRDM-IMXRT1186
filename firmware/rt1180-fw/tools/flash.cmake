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
# 쓰고 나면 리셋한다. MCU-Link 리셋은 J26 을 통해 POR 로 이어지므로 BootROM 부터
# 다시 돈다 (docs/03-board-mapping.md 2절).
#-------------------------------------------------------------------------------
find_program(PROBE_RS_EXECUTABLE NAMES probe-rs probe-rs.exe)

set(PROBE_RS_CHIP "MIMXRT1180" CACHE STRING "probe-rs chip name")

if(PROBE_RS_EXECUTABLE)
  set(_flash_elfs $<TARGET_FILE:${PRJ_NAME}-cm33.elf>)
  set(_flash_deps ${PRJ_NAME}-cm33.elf)

  add_custom_target(flash
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
