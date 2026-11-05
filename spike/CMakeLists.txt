cmake_minimum_required(VERSION 3.23)
project(hf_studio_spike CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OpenVINO GenAI archive; override with -DOPENVINO_ROOT=... if installed elsewhere.
set(OPENVINO_ROOT "C:/Intel/openvino_genai" CACHE PATH "OpenVINO GenAI install dir")
set(OpenVINO_DIR "${OPENVINO_ROOT}/runtime/cmake")
find_package(OpenVINOGenAI REQUIRED PATHS "${OPENVINO_ROOT}/runtime/cmake" NO_DEFAULT_PATH)

add_executable(spike src/main.cpp src/hf_download.cpp)
target_link_libraries(spike PRIVATE openvino::genai winhttp bcrypt)
target_compile_options(spike PRIVATE /utf-8 /W4 /permissive-)
target_compile_definitions(spike PRIVATE UNICODE _UNICODE)
