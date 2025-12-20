/*
 * @Desc: 数据发布模块实现
 * @Author: JacksonZhou
 * @Date: 2025/12/19
 */

#include "data_publisher.hpp"
#include <chrono>
#include <sstream>
#include <iomanip>
#include "logger.hpp"

namespace simple_middleware {

DataPublisher::DataPublisher(const std::string& topic, int interval_ms)
    : topic_(topic)
    , interval_ms_(interval_ms)
    , running_(false)
    , message_count_(0)
    , sequence_(0) {
    LOG_DEBUG("DataPublisher") << "创建DataPublisher，主题: " << topic_ 
                               << ", 间隔: " << interval_ms_ << "ms";
}

DataPublisher::~DataPublisher() {
    stop();
}

bool DataPublisher::start() {
    if (running_) {
        LOG_WARN("DataPublisher") << "DataPublisher已经在运行";
        return false;
    }

    running_ = true;
    message_count_ = 0;
    sequence_ = 0;

    thread_ = std::make_unique<std::thread>(&DataPublisher::publishThread, this);
    
    LOG_INFO("DataPublisher") << "DataPublisher启动成功，主题: " << topic_;
    return true;
}

void DataPublisher::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    
    thread_.reset();
    
    LOG_INFO("DataPublisher") << "DataPublisher已停止，主题: " << topic_ 
                              << ", 总共发布: " << message_count_ << " 条消息";
}

void DataPublisher::publishThread() {
    LOG_DEBUG("DataPublisher") << "发布线程启动，主题: " << topic_;

    while (running_) {
        // 生成测试数据
        std::string data = generateTestData();
        
        // 发布消息
        bool success = PubSubMiddleware::getInstance().publish(topic_, data);
        
        if (success) {
            message_count_++;
            LOG_DEBUG("DataPublisher") << "发布消息 #" << sequence_ 
                                       << " 到主题 " << topic_ 
                                       << ", 数据: " << data;
        } else {
            LOG_WARN("DataPublisher") << "发布消息失败，主题: " << topic_;
        }

        // 等待指定间隔
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms_));
    }

    LOG_DEBUG("DataPublisher") << "发布线程退出，主题: " << topic_;
}

std::string DataPublisher::generateTestData() {
    sequence_++;
    
    // 获取当前时间戳
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    // 生成JSON格式的测试数据
    std::ostringstream oss;
    oss << "{"
        << "\"sequence\":" << sequence_ << ","
        << "\"timestamp\":" << timestamp << ","
        << "\"topic\":\"" << topic_ << "\","
        << "\"data\":{"
        << "\"value\":" << (sequence_ % 100) << ","
        << "\"status\":\"" << (sequence_ % 2 == 0 ? "ok" : "warning") << "\""
        << "}"
        << "}";
    
    return oss.str();
}

}  // namespace simple_middleware
