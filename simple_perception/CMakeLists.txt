cmake_minimum_required(VERSION 3.10)
project(simple_perception)

set(CMAKE_CXX_STANDARD 17)

# --- 3rdparty 配置 ---
add_subdirectory(../3rdparty 3rdparty_build)

find_package(Threads REQUIRED)

# 包含路径 (注意使用 CMAKE_CURRENT_SOURCE_DIR 的相对路径)
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../install/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/json11/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../simple_middleware
)

# 链接库路径
link_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../install/lib
)

# 源文件
set(SOURCES
    src/main.cpp
    src/perception_component.cpp
)

# 可执行文件
add_executable(perception_node ${SOURCES})

# 链接依赖
target_link_libraries(perception_node
    simple_middleware_lib
    common_msgs_lib
    json11
    3rdparty_protobuf  # 使用内部编译的 protobuf target
    Threads::Threads
)
