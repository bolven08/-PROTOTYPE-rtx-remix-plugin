cmake_minimum_required(VERSION 3.20)
project(prototype_rtx CXX C)

set(CMAKE_CXX_STANDARD 17)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Build as 32-bit (Win32)! PROTOTYPE is a 32-bit game.")
endif()

set(MINHOOK_DIR "${CMAKE_SOURCE_DIR}/deps/minhook")
set(DXSDK_DIR "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)")

set(SOURCES
    src/dllmain.cpp
    src/prototype_hooks.cpp
    src/d3d9_proxy.cpp
    ${MINHOOK_DIR}/src/buffer.c
    ${MINHOOK_DIR}/src/hook.c
    ${MINHOOK_DIR}/src/trampoline.c
    ${MINHOOK_DIR}/src/hde/hde32.c
    ${MINHOOK_DIR}/src/hde/hde64.c
)

add_library(prototype_rtx SHARED ${SOURCES})

target_include_directories(prototype_rtx PRIVATE
    src/
    ${MINHOOK_DIR}/include
    ${MINHOOK_DIR}/src
    "${DXSDK_DIR}/Include"
)

target_link_directories(prototype_rtx PRIVATE
    "${DXSDK_DIR}/Lib/x86"
)

target_link_libraries(prototype_rtx PRIVATE
    d3d9
    d3dx9
    dxguid
    winmm
)

set_target_properties(prototype_rtx PROPERTIES
    OUTPUT_NAME "dinput8"
    SUFFIX ".dll"
    PREFIX ""
)

target_compile_definitions(prototype_rtx PRIVATE
    WIN32
    _WINDOWS
    _USRDLL
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)