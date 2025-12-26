/*
 * @Desc: 简易订阅发布中间件
 * @Author: JacksonZhou
 * @Date: 2025/12/19
 */

#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>

#include <thread>
#include <atomic>
#include <netinet/in.h>

namespace simple_middleware {

/**
 * @brief 消息数据类
 */
struct Message {
    std::string topic;      // 主题
    std::string data;       // 数据内容
    int64_t timestamp;      // 时间戳（毫秒）
    
    Message() : timestamp(0) {}
    Message(const std::string& t, const std::string& d) 
        : topic(t), data(d), timestamp(0) {}
};

/**
 * @brief 订阅回调函数类型
 */
using SubscribeCallback = std::function<void(const Message&)>;

/**
 * @brief 简易订阅发布中间件
 * @details 提供线程安全的订阅/发布功能
 */
class PubSubMiddleware {
public:
    /**
     * @brief 获取单例实例
     * 【注意：单例模式】保证全局只有一个中间件实例，方便在不同模块间共享通信状态
     */
    static PubSubMiddleware& getInstance() {
        static PubSubMiddleware instance;
        return instance;
    }

    /**
     * @brief 发布消息
     * @param topic 主题名称
     * @param data 消息数据
     * @return 是否发布成功
     */
    bool publish(const std::string& topic, const std::string& data);

    /**
     * @brief 订阅主题
     * @param topic 主题名称
     * @param callback 回调函数，当收到消息时调用
     * @return 订阅ID（可用于取消订阅），失败返回-1
     * 【注意：std::function】这允许传入任何可调用对象（函数指针、Lambda、std::bind等）
     */
    int64_t subscribe(const std::string& topic, SubscribeCallback callback);

    /**
     * @brief 取消订阅
     * @param subscribe_id 订阅ID
     * @return 是否成功
     */
    bool unsubscribe(int64_t subscribe_id);

    /**
     * @brief 取消某个主题的所有订阅
     * @param topic 主题名称
     * @return 取消的订阅数量
     */
    size_t unsubscribeTopic(const std::string& topic);

    /**
     * @brief 获取某个主题的订阅者数量
     * @param topic 主题名称
     * @return 订阅者数量
     */
    size_t getSubscriberCount(const std::string& topic) const;

    /**
     * @brief 获取所有主题列表
     * @return 主题列表
     */
    std::vector<std::string> getAllTopics() const;

private:
    PubSubMiddleware();
    ~PubSubMiddleware();
    PubSubMiddleware(const PubSubMiddleware&) = delete;
    PubSubMiddleware& operator=(const PubSubMiddleware&) = delete;

    // 仅分发到本地订阅者，不进行网络广播
    void dispatchLocal(const std::string& topic, const std::string& data);

    // UDP 接收线程
    void udpReceiveLoop();
    void initUdpSocket();

    // 订阅信息结构
    struct Subscription {
        int64_t id;
        std::string topic;
        SubscribeCallback callback;
    };

    mutable std::mutex mutex_;                                    // 互斥锁
    std::unordered_map<std::string, std::vector<int64_t>> topic_subscribers_;  // 主题 -> 订阅ID列表
    std::unordered_map<int64_t, Subscription> subscriptions_;     // 订阅ID -> 订阅信息
    int64_t next_subscribe_id_;                                    // 下一个订阅ID

    // 网络通信相关
    int udp_socket_fd_ = -1;
    struct sockaddr_in broadcast_addr_;
    std::thread receiver_thread_;
    std::atomic<bool> running_{false};
    static constexpr int UDP_PORT = 12345;
};

}  // namespace simple_middleware
