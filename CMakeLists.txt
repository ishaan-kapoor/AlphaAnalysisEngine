cmake_minimum_required(VERSION 3.16)
project(hft_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -O2)

find_package(Threads REQUIRED)

add_library(alpha_sdk INTERFACE)
target_include_directories(alpha_sdk INTERFACE src)
target_compile_features(alpha_sdk INTERFACE cxx_std_20)

file(GLOB ALPHA_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/alphas/*.cpp")
add_library(alpha_impls OBJECT ${ALPHA_SOURCES})
target_link_libraries(alpha_impls PRIVATE alpha_sdk)

add_executable(engine src/main.cpp src/alpha_registry.cpp src/config.cpp src/run.cpp)
target_include_directories(engine PRIVATE src third_party)
target_link_libraries(engine PRIVATE alpha_impls Threads::Threads)