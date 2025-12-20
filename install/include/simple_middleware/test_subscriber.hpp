/*
 * @Desc: 测试订阅者 - 用于测试中间件功能
 * @Author: JacksonZhou
 * @Date: 2025/12/19
 */

#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include "pub_sub_middleware.hpp"

namespace simple_middleware {

/**
 * @brief 测试订阅者类
 * @details 订阅主题并统计接收到的消息
 */
class TestSubscriber {
public:
    /**
     * @brief 构造函数
     * @param topic 订阅的主题
     */
    explicit TestSubscriber(const std::string& topic);

    /**
     * @brief 析构函数
     */
    ~TestSubscriber();

    /**
     * @brief 开始订阅
     * @return 是否成功
     */
    bool start();

    /**
     * @brief 停止订阅
     */
    void stop();

    /**
     * @brief 获取接收到的消息数量
     */
    uint64_t getMessageCount() const { return message_count_; }

    /**
     * @brief 获取最后一次接收到的消息
     */
    std::string getLastMessage() const;

private:
    /**
     * @brief 消息回调函数
     */
    void onMessage(const Message& msg);

    std::string topic_;                    // 订阅主题
    int64_t subscribe_id_;                 // 订阅ID
    std::atomic<bool> subscribed_;         // 是否已订阅
    std::atomic<uint64_t> message_count_;  // 消息计数
    std::string last_message_;             // 最后一次消息
    mutable std::mutex last_msg_mutex_;    // 保护last_message_的互斥锁
};

}  // namespace simple_middleware
