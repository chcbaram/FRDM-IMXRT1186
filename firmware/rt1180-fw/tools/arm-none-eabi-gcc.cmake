
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)


set(ARM_POSSIBLE_PATHS
    ENV ARM_TOOLCHAIN_DIR
    "C:/work/tools/baram-fw-tools/arm_toolchain/arm_gcc/test"
)

set(MAKE_POSSIBLE_PATHS
    "c:/MinGW-32/bin"
    ENV MAKE_DIR    
)

find_program(ARM_TOOLCHAIN_DIR
    NAMES arm-none-eabi-gcc.exe arm-none-eabi-gcc
    HINTS ${ARM_POSSIBLE_PATHS}
    PATH_SUFFIXES bin
    DOC "ARM GCC Toolchain Directory"
)

if(NOT ARM_TOOLCHAIN_DIR)
    message(FATAL_ERROR "ARM Toolchain not found. Please set ARM_TOOLCHAIN_DIR environment variable")
endif()



# CMAKE_MAKE_PROGRAM 은 제너레이터가 쓰는 빌드 도구다.
# Ninja 를 쓰면 CMake 가 여기서 ninja 를 기대하므로, make 를 넣으면 안 된다.
# Makefile 계열 제너레이터일 때만 make 를 찾아준다. (기본은 Ninja)
#
if(CMAKE_GENERATOR MATCHES "Makefiles")
    find_program(CMAKE_MAKE_PROGRAM
      NAMES make
            make.exe
      DOC "Find a suitable make program for building under Windows/MinGW"
      HINTS ${MAKE_POSSIBLE_PATHS}
    )

    if(NOT CMAKE_MAKE_PROGRAM)
        message(FATAL_ERROR "Make program not found. Please set MAKE_DIR environment variable")
    else()
        message(STATUS "Found Make program: ${CMAKE_MAKE_PROGRAM}")
    endif()
endif()


# ARM_TOOLCHAIN_DIR에서 실행 파일 이름을 제거하고 경로만 추출  
get_filename_component(TOOLCHAIN_PATH "${ARM_TOOLCHAIN_DIR}" DIRECTORY)
set(TOOLCHAIN_PREFIX "${TOOLCHAIN_PATH}/arm-none-eabi-")



set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if (WIN32)
set(CMAKE_C_COMPILER "${TOOLCHAIN_PREFIX}gcc.exe" CACHE FILEPATH "C Compiler path")
set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++.exe" CACHE FILEPATH "C++ Compiler path")
else()
set(CMAKE_C_COMPILER "${TOOLCHAIN_PREFIX}gcc" CACHE FILEPATH "C Compiler path")
set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++" CACHE FILEPATH "C++ Compiler path")
endif()

set(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy CACHE INTERNAL "objcopy tool")
set(CMAKE_SIZE_UTIL ${TOOLCHAIN_PREFIX}size CACHE INTERNAL "size tool")

set(CMAKE_C_STANDARD    11)
set(CMAKE_CXX_STANDARD  17)

# Disable compiler checks.
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

set(CMAKE_FIND_ROOT_PATH ${BINUTILS_PATH})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)