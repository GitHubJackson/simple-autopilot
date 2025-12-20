/*
 * @Desc: 测试订阅者实现
 * @Author: JacksonZhou
 * @Date: 2025/12/19
 */

#include "test_subscriber.hpp"
#include "logger.hpp"

namespace simple_middleware {

TestSubscriber::TestSubscriber(const std::string& topic)
    : topic_(topic)
    , subscribe_id_(-1)
    , subscribed_(false)
    , message_count_(0) {
    LOG_DEBUG("TestSubscriber") << "创建TestSubscriber，主题: " << topic_;
}

TestSubscriber::~TestSubscriber() {
    stop();
}

bool TestSubscriber::start() {
    if (subscribed_) {
        LOG_WARN("TestSubscriber") << "TestSubscriber已经订阅";
        return false;
    }

    // 订阅主题
    subscribe_id_ = PubSubMiddleware::getInstance().subscribe(
        topic_,
        [this](const Message& msg) { this->onMessage(msg); }
    );

    if (subscribe_id_ < 0) {
        LOG_ERROR("TestSubscriber") << "订阅失败，主题: " << topic_;
        return false;
    }

    subscribed_ = true;
    message_count_ = 0;
    
    LOG_INFO("TestSubscriber") << "TestSubscriber订阅成功，主题: " << topic_ 
                               << ", 订阅ID: " << subscribe_id_;
    return true;
}

void TestSubscriber::stop() {
    if (!subscribed_) {
        return;
    }

    if (subscribe_id_ >= 0) {
        PubSubMiddleware::getInstance().unsubscribe(subscribe_id_);
        subscribe_id_ = -1;
    }

    subscribed_ = false;
    
    LOG_INFO("TestSubscriber") << "TestSubscriber取消订阅，主题: " << topic_ 
                               << ", 总共接收: " << message_count_ << " 条消息";
}

void TestSubscriber::onMessage(const Message& msg) {
    message_count_++;
    
    {
        std::lock_guard<std::mutex> lock(last_msg_mutex_);
        last_message_ = msg.data;
    }
    
    LOG_DEBUG("TestSubscriber") << "收到消息 #" << message_count_ 
                                << ", 主题: " << msg.topic 
                                << ", 数据: " << msg.data 
                                << ", 时间戳: " << msg.timestamp;
}

std::string TestSubscriber::getLastMessage() const {
    std::lock_guard<std::mutex> lock(last_msg_mutex_);
    return last_message_;
}

}  // namespace simple_middleware
