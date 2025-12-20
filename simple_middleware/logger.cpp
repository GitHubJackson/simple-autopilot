/*
 * @Desc: 简单日志工具实现
 * @Author: JacksonZhou
 * @Date: 2025/12/19
 */

#include "logger.hpp"

namespace simple_middleware {

// 静态成员定义
LogLevel Logger::min_level_ = LogLevel::INFO;
std::mutex Logger::mutex_;

}  // namespace simple_middleware
