cmake_minimum_required(VERSION 3.10)
project(common_msgs)

set(CMAKE_CXX_STANDARD 17)

# --- 3rdparty 配置 ---
# 我们需要引用 3rdparty 来获取 Protobuf 配置
# 假设上一级目录的 3rdparty 已经配置好
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/cmake")

include(FindProtobufWrapper)

# --- 生成 Protobuf 代码 ---
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(PROTO_SRCS "")
set(PROTO_HDRS "")

# 获取所有 proto 文件
file(GLOB PROTO_FILES "${PROTO_DIR}/*.proto")

# 由于 FindProtobufWrapper 已经处理了 protoc 的查找，这里我们手动生成
# 为了让生成的头文件引用路径比较干净（例如 #include "visualizer_data.pb.h"），
# 我们把它们生成到 build 目录的根部，或者专门的 include 目录

# 这里为了简单，生成到 CMAKE_CURRENT_BINARY_DIR
foreach(PROTO_FILE ${PROTO_FILES})
    get_filename_component(FIL_WE ${PROTO_FILE} NAME_WE)
    
    set(PB_SRC "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.pb.cc")
    set(PB_HDR "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.pb.h")
    
    add_custom_command(
        OUTPUT ${PB_SRC} ${PB_HDR}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        ARGS --cpp_out ${CMAKE_CURRENT_BINARY_DIR} -I ${PROTO_DIR} ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
        COMMENT "Running C++ protocol buffer compiler on ${PROTO_FILE}"
        VERBATIM
    )
    
    list(APPEND PROTO_SRCS ${PB_SRC})
    list(APPEND PROTO_HDRS ${PB_HDR})
endforeach()

# --- 创建库 ---
add_library(common_msgs_lib SHARED ${PROTO_SRCS})

# 链接 Protobuf
target_link_libraries(common_msgs_lib 3rdparty_protobuf)

# --- 安装配置 ---
include(GNUInstallDirs)

# 安装库
install(TARGETS common_msgs_lib
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# 安装生成的头文件
install(FILES ${PROTO_HDRS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/common_msgs
)

# 安装手动编写的头文件 (simple_image.hpp)
install(FILES simple_image.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/common_msgs
)
