# 统一管理 Protobuf 依赖
# 在实际项目中，这里可能会下载源码并编译，或者指定具体的搜索路径
# 这里我们封装 find_package，统一管理版本和参数

# 尝试查找 Protobuf
find_package(Protobuf REQUIRED)

if(NOT Protobuf_PROTOC_EXECUTABLE)
    # 显式查找，解决 CMake 找不到的问题
    find_program(Protobuf_PROTOC_EXECUTABLE protoc
        PATHS /opt/homebrew/bin /usr/local/bin /usr/bin
    )
endif()

message(STATUS "Protobuf compiler: ${Protobuf_PROTOC_EXECUTABLE}")

# 确保找到了 protoc
if(NOT Protobuf_PROTOC_EXECUTABLE AND TARGET protobuf::protoc)
    set(Protobuf_PROTOC_EXECUTABLE protobuf::protoc)
endif()
if(NOT Protobuf_PROTOC_EXECUTABLE)
    find_program(Protobuf_PROTOC_EXECUTABLE protoc)
endif()

# 尝试查找 Abseil (新版 Protobuf 依赖)
find_package(absl QUIET)

# 定义一个统一的 INTERFACE 库，供项目内部使用
# 这样其他模块只需要 target_link_libraries(my_target 3rdparty_protobuf)
# 不需要关心具体的 include_directories 和 link_libraries 细节
add_library(3rdparty_protobuf INTERFACE)

target_include_directories(3rdparty_protobuf INTERFACE
    ${Protobuf_INCLUDE_DIRS}
)

if(UNIX)
    target_link_libraries(3rdparty_protobuf INTERFACE
        ${Protobuf_LIBRARIES}
        pthread
    )
    # 如果找到了 absl，也链接进去
    if(absl_FOUND)
        target_link_libraries(3rdparty_protobuf INTERFACE
            absl::base
            absl::strings
            absl::log_internal_message
            absl::log_internal_check_op
            absl::status
        )
    endif()
else()
    target_link_libraries(3rdparty_protobuf INTERFACE ${Protobuf_LIBRARIES})
endif()

message(STATUS "3rdparty: Protobuf configured via 3rdparty_protobuf target")
