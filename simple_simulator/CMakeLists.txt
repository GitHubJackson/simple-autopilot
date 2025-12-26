cmake_minimum_required(VERSION 3.10)
project(simple_simulator)

set(CMAKE_CXX_STANDARD 17)

# 依赖
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../install/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/json11/include)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../install/lib)

# --- 3rdparty 配置 ---
add_subdirectory(../3rdparty 3rdparty_build)

# 源文件
set(SOURCES
    src/main.cpp
    src/simulator_core.cpp
)

# 可执行文件
add_executable(simulator_node ${SOURCES})

# 链接库
target_link_libraries(simulator_node
    simple_middleware_lib
    common_msgs_lib
    3rdparty_protobuf
    pthread
    json11
)

# 输出目录
set_target_properties(simulator_node PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

