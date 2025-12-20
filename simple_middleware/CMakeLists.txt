cmake_minimum_required(VERSION 3.10)
project(simple_middleware)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread")

# 源文件
set(SOURCES
    pub_sub_middleware.cpp
    data_publisher.cpp
    test_subscriber.cpp
    logger.cpp
)

# 头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# 创建动态库
add_library(simple_middleware_lib SHARED ${SOURCES})

# 安装配置
include(GNUInstallDirs)

# 安装库文件
install(TARGETS simple_middleware_lib
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# 安装头文件
install(FILES 
    pub_sub_middleware.hpp 
    data_publisher.hpp 
    test_subscriber.hpp 
    logger.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/simple_middleware
)

# 监视器程序也作为工具安装
add_executable(middleware_monitor monitor.cpp)
target_link_libraries(middleware_monitor simple_middleware_lib pthread)
install(TARGETS middleware_monitor DESTINATION ${CMAKE_INSTALL_BINDIR})

# 测试程序
add_executable(test_middleware test_main.cpp)
target_link_libraries(test_middleware simple_middleware_lib pthread)

# 设置输出目录
set_target_properties(test_middleware PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

message(STATUS "Simple Middleware project configured successfully")
message(STATUS "Build test program: make test_middleware")
message(STATUS "Run test: ./bin/test_middleware")
