cmake_minimum_required(VERSION 3.10)
project(json11)

add_library(json11 STATIC src/json11.cpp)

target_include_directories(json11 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# 安装规则
install(TARGETS json11 EXPORT json11_targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)

