cmake_minimum_required(VERSION 3.10)
project(simple_map)

set(CMAKE_CXX_STANDARD 17)

# --- 3rdparty 配置 ---
add_subdirectory(../3rdparty 3rdparty_build)

# --- Common Msgs ---
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../install/lib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../install/include)

# --- Middleware ---
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../install/lib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../install/include)

# --- Source ---
include_directories(src)

set(SOURCES
    src/main.cpp
    src/map_component.cpp
)

add_executable(map_server ${SOURCES})

# 链接库
if(UNIX)
    target_link_libraries(map_server 
        simple_middleware_lib
        common_msgs_lib
        3rdparty_protobuf
        json11
        dl
        pthread
    )
else()
    target_link_libraries(map_server simple_middleware_lib common_msgs_lib 3rdparty_protobuf json11)
endif()
