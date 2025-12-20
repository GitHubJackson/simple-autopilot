cmake_minimum_required(VERSION 3.10)
project(system_monitor)

set(CMAKE_CXX_STANDARD 17)

# --- 3rdparty 配置 (为了获取 Protobuf target) ---
add_subdirectory(../3rdparty 3rdparty_build)

# --- Include Directories ---
# 包含安装目录的头文件 (simple_middleware, common_msgs)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../install/include)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../install/lib)

# --- Executable ---
add_executable(monitor src/monitor.cpp)

# --- Libraries ---
if(UNIX)
    target_link_libraries(monitor 
        simple_middleware_lib
        common_msgs_lib
        3rdparty_protobuf
        pthread
        dl
    )
else()
    target_link_libraries(monitor 
        simple_middleware_lib 
        common_msgs_lib
        3rdparty_protobuf
        pthread
    )
endif()

# --- Install ---
include(GNUInstallDirs)
install(TARGETS monitor DESTINATION ${CMAKE_INSTALL_BINDIR})

