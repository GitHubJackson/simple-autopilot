/*
 * @Desc: 数据发布模块 - 定时发送测试数据
 * @Author: JacksonZhou
 * @Date: 2025/12/19
 */

#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include "pub_sub_middleware.hpp"

namespace simple_middleware {

/**
 * @brief 数据发布器
 * @details 定时向中间件发布测试数据
 */
class DataPublisher {
public:
    /**
     * @brief 构造函数
     * @param topic 发布主题
     * @param interval_ms 发布间隔（毫秒），默认1000ms
     */
    DataPublisher(const std::string& topic, int interval_ms = 1000);

    /**
     * @brief 析构函数
     */
    ~DataPublisher();

    /**
     * @brief 启动发布线程
     * @return 是否启动成功
     */
    bool start();

    /**
     * @brief 停止发布线程
     */
    void stop();

    /**
     * @brief 是否正在运行
     */
    bool isRunning() const { return running_; }

    /**
     * @brief 设置发布间隔
     * @param interval_ms 间隔（毫秒）
     */
    void setInterval(int interval_ms) { interval_ms_ = interval_ms; }

    /**
     * @brief 获取发布间隔
     */
    int getInterval() const { return interval_ms_; }

    /**
     * @brief 获取发布的消息计数
     */
    uint64_t getMessageCount() const { return message_count_; }

private:
    /**
     * @brief 发布线程函数
     */
    void publishThread();

    /**
     * @brief 生成测试数据
     * @return 测试数据字符串（JSON格式）
     */
    std::string generateTestData();

    std::string topic_;              // 发布主题
    int interval_ms_;                // 发布间隔（毫秒）
    std::atomic<bool> running_;      // 运行标志
    std::atomic<uint64_t> message_count_;  // 消息计数
    std::unique_ptr<std::thread> thread_;   // 发布线程
    uint64_t sequence_;              // 序列号
};

}  // namespace simple_middleware
