/*
 * @Desc: 简单日志工具
 * @Author: JacksonZhou
 * @Date: 2025/12/19
 */

#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <mutex>

namespace simple_middleware {

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    Logger(const std::string& name, LogLevel level = LogLevel::INFO)
        : name_(name), level_(level) {}

    template<typename T>
    Logger& operator<<(const T& msg) {
        if (shouldLog()) {
            buffer_ << msg;
        }
        return *this;
    }

    ~Logger() {
        if (shouldLog() && !buffer_.str().empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            std::cerr << "[" << getLevelString() << "] [" << name_ << "] " 
                      << buffer_.str() << std::endl;
        }
    }

private:
    bool shouldLog() const {
        return level_ >= min_level_;
    }

    std::string getLevelString() const {
        switch (level_) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::ERROR: return "ERROR";
            default: return "UNKNOWN";
        }
    }

    static LogLevel min_level_;
    std::string name_;
    LogLevel level_;
    std::ostringstream buffer_;
    static std::mutex mutex_;
};

// 便捷宏定义
#define LOG_DEBUG(name) Logger(name, LogLevel::DEBUG)
#define LOG_INFO(name)  Logger(name, LogLevel::INFO)
#define LOG_WARN(name)  Logger(name, LogLevel::WARN)
#define LOG_ERROR(name) Logger(name, LogLevel::ERROR)

}  // namespace simple_middleware
