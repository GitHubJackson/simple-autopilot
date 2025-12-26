# 统一管理 Protobuf 依赖
# 优先查找项目本地 install 目录下的 Protobuf

# 1. 设置搜索路径，优先查找本地 install 目录
# 注意：CMAKE_SOURCE_DIR 指的是当前正在配置的子项目的源码目录（例如 simple-autopilot/common_msgs）
# 而我们的 install 目录位于项目根目录 (simple-autopilot/install)
# 假设当前模块都在 simple-autopilot/* 一级目录下，那么项目根目录就是上一级
get_filename_component(PROJECT_ROOT_DIR "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
set(LOCAL_INSTALL_DIR "${PROJECT_ROOT_DIR}/install")

# 如果没找到，再尝试一下当前目录（兼容根目录直接构建的情况）
if(NOT EXISTS "${LOCAL_INSTALL_DIR}")
    set(LOCAL_INSTALL_DIR "${CMAKE_SOURCE_DIR}/install")
endif()

list(PREPEND CMAKE_PREFIX_PATH "${LOCAL_INSTALL_DIR}")

message(STATUS "3rdparty: Looking for Protobuf in ${LOCAL_INSTALL_DIR} first...")

# 2. 查找 Protobuf (CONFIG 模式优先查找 protobuf-config.cmake)
find_package(Protobuf CONFIG)

# 如果 CONFIG 模式没找到（可能没编译或者版本不对），尝试 MODULE 模式（找系统库）
if(NOT Protobuf_FOUND)
    message(WARNING "Protobuf config not found in local install, trying system default...")
    find_package(Protobuf REQUIRED)
endif()

# 3. 强制指定 protoc 可执行文件路径
# 我们不信任 find_package 返回的结果，因为可能有多个版本共存
# 直接强制指向本地 install 目录下的 protoc
set(Protobuf_PROTOC_EXECUTABLE "${LOCAL_INSTALL_DIR}/bin/protoc")

if(NOT EXISTS "${Protobuf_PROTOC_EXECUTABLE}")
    message(WARNING "Local protoc not found at ${Protobuf_PROTOC_EXECUTABLE}, falling back to search...")
    find_program(Protobuf_PROTOC_EXECUTABLE protoc
        PATHS /opt/homebrew/bin /usr/local/bin /usr/bin
    )
endif()

message(STATUS "Protobuf compiler (forced): ${Protobuf_PROTOC_EXECUTABLE}")

# --- 强制版本检查 ---
execute_process(COMMAND ${Protobuf_PROTOC_EXECUTABLE} --version OUTPUT_VARIABLE PROTOC_VERSION_OUTPUT)
message(STATUS "Protobuf compiler version: ${PROTOC_VERSION_OUTPUT}")

if(PROTOC_VERSION_OUTPUT MATCHES "3.19.4")
    message(STATUS "Protobuf version match confirmed.")
else()
    message(WARNING "Protobuf compiler version mismatch! Expected 3.19.4, got ${PROTOC_VERSION_OUTPUT}. This may cause ABI issues.")
    # 如果不是为了兼容旧环境，这里其实应该报错 FATAL_ERROR
endif()
# ------------------

message(STATUS "Protobuf library: ${Protobuf_LIBRARIES}")
message(STATUS "Protobuf include: ${Protobuf_INCLUDE_DIRS}")

# 4. 定义统一接口库
add_library(3rdparty_protobuf INTERFACE)

# 注意：如果是 CONFIG 模式找到的，通常建议直接链接 protobuf::libprotobuf
if(TARGET protobuf::libprotobuf)
     target_link_libraries(3rdparty_protobuf INTERFACE protobuf::libprotobuf)
else()
    # 传统模式
    target_include_directories(3rdparty_protobuf INTERFACE ${Protobuf_INCLUDE_DIRS})
    target_link_libraries(3rdparty_protobuf INTERFACE ${Protobuf_LIBRARIES})
    if(UNIX)
        target_link_libraries(3rdparty_protobuf INTERFACE pthread)
    endif()
endif()

message(STATUS "3rdparty: Protobuf configured via 3rdparty_protobuf target")
