cmake_minimum_required(VERSION 3.22.1)
project(companion_native CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Turn on once llama.cpp is added as a submodule at app/src/main/cpp/third_party/llama.cpp
option(COMPANION_WITH_LLAMA "Link llama.cpp as the LLM backend" OFF)

add_library(companion_native SHARED
    jni_bridge.cpp
    model_manager.cpp
    stub_engine.cpp)

if(COMPANION_WITH_LLAMA)
    set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
    set(LLAMA_CURL OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp ${CMAKE_BINARY_DIR}/llama)
    target_sources(companion_native PRIVATE llama_engine.cpp)
    target_compile_definitions(companion_native PRIVATE COMPANION_WITH_LLAMA=1)
    target_link_libraries(companion_native PRIVATE llama)
endif()

find_library(log-lib log)
target_link_libraries(companion_native PRIVATE ${log-lib})
