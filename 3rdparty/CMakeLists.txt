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
    message(STATUS "3rdparty: CivetWeb configured from local source")
else()
    # 尝试查找系统安装的 CivetWeb
    find_path(CIVETWEB_INCLUDE_DIR CivetServer.h
        PATHS
        /usr/include
        /usr/local/include
        /opt/homebrew/include
        ${CMAKE_CURRENT_SOURCE_DIR}/civetweb/include
    )
    
    find_library(CIVETWEB_CPP_LIB civetweb-cpp
        PATHS
        /usr/lib
        /usr/local/lib
        /opt/homebrew/lib
    )
    
    find_library(CIVETWEB_C_LIB civetweb-c-library
        PATHS
        /usr/lib
        /usr/local/lib
        /opt/homebrew/lib
    )
    
    if(CIVETWEB_INCLUDE_DIR AND CIVETWEB_CPP_LIB AND CIVETWEB_C_LIB)
        add_library(3rdparty_civetweb INTERFACE)
        target_include_directories(3rdparty_civetweb INTERFACE ${CIVETWEB_INCLUDE_DIR})
        target_link_libraries(3rdparty_civetweb INTERFACE ${CIVETWEB_CPP_LIB} ${CIVETWEB_C_LIB})
        message(STATUS "3rdparty: CivetWeb configured from system installation")
        message(STATUS "  Include: ${CIVETWEB_INCLUDE_DIR}")
        message(STATUS "  Libraries: ${CIVETWEB_CPP_LIB} ${CIVETWEB_C_LIB}")
    else()
        message(FATAL_ERROR "CivetWeb not found! Please either:\n"
            "  1. Clone CivetWeb to 3rdparty/civetweb:\n"
            "     cd 3rdparty && git clone https://github.com/civetweb/civetweb.git\n"
            "  2. Install CivetWeb system-wide (e.g., apt-get install libcivetweb-dev)\n"
            "     Include dir: ${CIVETWEB_INCLUDE_DIR}\n"
            "     C++ lib: ${CIVETWEB_CPP_LIB}\n"
            "     C lib: ${CIVETWEB_C_LIB}")
    endif()
endif()

# --- json11 ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/json11/CMakeLists.txt")
    add_subdirectory(json11)
    message(STATUS "3rdparty: json11 configured")
endif()
