cmake_minimum_required(VERSION 3.10)
project(MyCivetWebServer)

set(CMAKE_CXX_STANDARD 17)

# --- 3rdparty 配置 ---
add_subdirectory(../3rdparty 3rdparty_build)

# --- Middleware ---
# 假设 simple_middleware 安装在 ../install 目录
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../install/lib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../install/include)

# --- Protobuf 配置 ---
# 旧方式已移除，直接使用 3rdparty 提供的 target
# include_directories(${Protobuf_INCLUDE_DIRS})
# include_directories(${CMAKE_CURRENT_BINARY_DIR})

# --- Common Msgs ---
# 假设 common_msgs 安装在 ../install 目录 (与 middleware 一样)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../install/lib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../install/include)

# --- CivetWeb 配置 ---
# 已移至 3rdparty 统一管理
# include_directories(lib/civetweb/include)

include_directories(src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/json11/include)

set(SOURCE_FILES
    src/main.cpp
    src/server/visualizer_server.cpp
    src/handler/websocket_handler.cpp
    src/component/visualizer_component.cpp
)

add_executable(server ${SOURCE_FILES})

# 拷贝 www 目录到构建目录
add_custom_command(TARGET server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/www
    ${CMAKE_CURRENT_BINARY_DIR}/www
    COMMENT "Copying www directory to build directory"
)

# 链接库
# 注意：新版 Protobuf 强烈建议链接 abseil 的所有库，或者使用 absl::base 等
if(UNIX)
    target_link_libraries(server 
        simple_middleware_lib
        common_msgs_lib    # 链接公共消息库
        3rdparty_protobuf  # 使用统一的 3rdparty target
        3rdparty_civetweb  # 使用统一的 3rdparty target
        json11             # json11 库
        dl
    )
else()
    target_link_libraries(server 
        simple_middleware_lib 
        common_msgs_lib
        3rdparty_protobuf 
        3rdparty_civetweb
        json11             # json11 库
    )
endif()
