cmake_minimum_required(VERSION 3.25)

project(FrameStepPPWeb LANGUAGES CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "The FrameStep++ playground must be built with Emscripten")
endif()

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(FRAMESTEPP_ENABLE_SANITIZERS OFF CACHE BOOL "" FORCE)

add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    "${CMAKE_CURRENT_BINARY_DIR}/framestepp"
    EXCLUDE_FROM_ALL
)

add_executable(framestepp_web bridge.cpp)
target_link_libraries(framestepp_web PRIVATE FrameStepPP::core)
framestepp_configure_target(framestepp_web)

# Emscripten disables C++ exception catching by default in optimized builds.
# The FrameStep++ VM deliberately catches allocation and stream failures.
target_compile_options(framestepp_core PRIVATE -fexceptions)
target_compile_options(framestepp_web PRIVATE -fexceptions)

target_link_options(framestepp_web PRIVATE
    --bind
    --no-entry
    -fexceptions
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sEXPORT_NAME=createFrameStepModule"
    "-sENVIRONMENT=web,worker,node"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sFILESYSTEM=0"
)

set_target_properties(framestepp_web PROPERTIES
    OUTPUT_NAME framestepp
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../public/wasm"
)
