cmake_minimum_required(VERSION 3.10)
project(simple_control)

set(CMAKE_CXX_STANDARD 17)

# --- 3rdparty 配置 ---
add_subdirectory(../3rdparty 3rdparty_build)

# --- Protobuf 配置 ---
# 旧方式已移除，直接使用 3rdparty 提供的 target
# include_directories(${Protobuf_INCLUDE_DIRS})
# include_directories(${CMAKE_CURRENT_BINARY_DIR})

# --- Common Msgs ---
# 假设 common_msgs 安装在 ../install 目录 (与 middleware 一样)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../install/lib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../install/include)

# --- Middleware ---
# 假设 simple_middleware 安装在 ../install 目录
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../install/lib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../install/include)

# --- Source ---
include_directories(src)

set(SOURCES
    src/main.cpp
    src/control_component.cpp
)

add_executable(control_server ${SOURCES})

# 链接库
if(UNIX)
    target_link_libraries(control_server 
        simple_middleware_lib
        common_msgs_lib    # 链接公共消息库
        3rdparty_protobuf  # 使用统一的 3rdparty target
        dl
    )
else()
    target_link_libraries(control_server simple_middleware_lib common_msgs_lib 3rdparty_protobuf)
endif()
