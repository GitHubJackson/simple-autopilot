#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <iostream>
#include <memory>
#include <sstream>

namespace simple_middleware {

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    static Logger& GetInstance() {
        static Logger instance;
        return instance;
    }

    void Init(const std::string& module_name, const std::string& log_file_path = "");
    void Log(LogLevel level, const std::string& message);

    static void Info(const std::string& msg) { GetInstance().Log(LogLevel::INFO, msg); }
    static void Warn(const std::string& msg) { GetInstance().Log(LogLevel::WARN, msg); }
    static void Error(const std::string& msg) { GetInstance().Log(LogLevel::ERROR, msg); }
    static void Debug(const std::string& msg) { GetInstance().Log(LogLevel::DEBUG, msg); }

private:
    Logger() = default;
    ~Logger();
    
    // 禁止拷贝
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string module_name_;
    std::ofstream log_file_;
    std::mutex mutex_;
    bool initialized_ = false;
};

// 日志流类，支持 << 操作符
class LogStream {
public:
    LogStream(LogLevel level, const std::string& module_name)
        : level_(level), module_name_(module_name) {
        if (!module_name_.empty()) {
            stream_ << "[" << module_name_ << "] ";
        }
    }
    
    ~LogStream() {
        Logger::GetInstance().Log(level_, stream_.str());
    }
    
    // 禁止拷贝
    LogStream(const LogStream&) = delete;
    LogStream& operator=(const LogStream&) = delete;
    
    // 支持移动
    LogStream(LogStream&&) = default;
    LogStream& operator=(LogStream&&) = default;
    
    template<typename T>
    LogStream& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }

private:
    LogLevel level_;
    std::string module_name_;
    std::ostringstream stream_;
};

// 日志宏定义
#define LOG_DEBUG(module) simple_middleware::LogStream(simple_middleware::LogLevel::DEBUG, module)
#define LOG_INFO(module) simple_middleware::LogStream(simple_middleware::LogLevel::INFO, module)
#define LOG_WARN(module) simple_middleware::LogStream(simple_middleware::LogLevel::WARN, module)
#define LOG_ERROR(module) simple_middleware::LogStream(simple_middleware::LogLevel::ERROR, module)

} // namespace simple_middleware
