cmake_minimum_required(VERSION 3.10)
project(3rdparty_libs)

# 将本目录下的 cmake 文件夹加入模块搜索路径
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- Protobuf ---
include(FindProtobufWrapper)

# --- CivetWeb ---
set(CIVETWEB_ENABLE_CXX ON CACHE BOOL "Enable C++ wrapper")
set(CIVETWEB_BUILD_TESTING OFF CACHE BOOL "Disable testing")
set(CIVETWEB_ENABLE_WEBSOCKETS ON CACHE BOOL "Enable Websockets")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/civetweb/CMakeLists.txt")
    add_subdirectory(civetweb)
    
    add_library(3rdparty_civetweb INTERFACE)
    target_include_directories(3rdparty_civetweb INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/civetweb/include
    )
    target_link_libraries(3rdparty_civetweb INTERFACE
        civetweb-cpp
        civetweb-c-library
    )
    message(STATUS "3rdparty: CivetWeb configured")
endif()

# --- json11 ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/json11/CMakeLists.txt")
    add_subdirectory(json11)
    message(STATUS "3rdparty: json11 configured")
endif()
