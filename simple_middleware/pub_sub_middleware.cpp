/*
 * @Desc: 简易订阅发布中间件实现（UDP广播版）
 * @Author: JacksonZhou
 * @Date: 2025/12/19
 */

#include "pub_sub_middleware.hpp"
#include <chrono>
#include <algorithm>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include "logger.hpp"

namespace simple_middleware {

PubSubMiddleware::PubSubMiddleware() : next_subscribe_id_(1) {
    initUdpSocket();
    running_ = true;
    receiver_thread_ = std::thread(&PubSubMiddleware::udpReceiveLoop, this);
}

PubSubMiddleware::~PubSubMiddleware() {
    running_ = false;
    if (udp_socket_fd_ >= 0) {
        close(udp_socket_fd_);
        udp_socket_fd_ = -1;
    }
    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
    }
}

void PubSubMiddleware::initUdpSocket() {
    // 创建 UDP socket
    udp_socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket_fd_ < 0) {
        LOG_ERROR("PubSubMiddleware") << "创建 socket 失败";
        return;
    }

    // 设置广播权限
    int broadcast = 1;
    if (setsockopt(udp_socket_fd_, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        LOG_ERROR("PubSubMiddleware") << "设置广播权限失败";
        close(udp_socket_fd_);
        return;
    }

    // 设置地址重用
    int reuse = 1;
    if (setsockopt(udp_socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        LOG_ERROR("PubSubMiddleware") << "设置地址重用失败";
    }
    
    #ifdef SO_REUSEPORT
    if (setsockopt(udp_socket_fd_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
        LOG_ERROR("PubSubMiddleware") << "设置端口重用失败";
    }
    #endif

    // 绑定端口
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(UDP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(udp_socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("PubSubMiddleware") << "绑定端口失败";
        close(udp_socket_fd_);
        udp_socket_fd_ = -1;
        return;
    }

    // 设置发送目标地址 (255.255.255.255)
    memset(&broadcast_addr_, 0, sizeof(broadcast_addr_));
    broadcast_addr_.sin_family = AF_INET;
    broadcast_addr_.sin_port = htons(UDP_PORT);
    broadcast_addr_.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    LOG_INFO("PubSubMiddleware") << "UDP广播服务已启动，端口: " << UDP_PORT;
}

void PubSubMiddleware::udpReceiveLoop() {
    char buffer[65535]; 
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    while (running_) {
        if (udp_socket_fd_ < 0) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        ssize_t len = recvfrom(udp_socket_fd_, buffer, sizeof(buffer) - 1, 0, 
                               (struct sockaddr*)&sender_addr, &sender_len);
        
        if (len > 0) {
            buffer[len] = '\0';
            std::string raw_data(buffer, len);
            
            // 协议: topic|data
            size_t sep_pos = raw_data.find('|');
            if (sep_pos != std::string::npos) {
                std::string topic = raw_data.substr(0, sep_pos);
                std::string data = raw_data.substr(sep_pos + 1);
                
                // 分发到本地订阅者
                dispatchLocal(topic, data);
            }
        }
    }
}

void PubSubMiddleware::dispatchLocal(const std::string& topic, const std::string& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    Message msg(topic, data);
    msg.timestamp = timestamp;

    auto it = topic_subscribers_.find(topic);
    if (it != topic_subscribers_.end()) {
        for (int64_t sub_id : it->second) {
            auto sub_it = subscriptions_.find(sub_id);
            if (sub_it != subscriptions_.end()) {
                try {
                    sub_it->second.callback(msg);
                } catch (...) {}
            }
        }
    }
}

bool PubSubMiddleware::publish(const std::string& topic, const std::string& data) {
    if (topic.empty()) return false;

    // 1. 本地分发 
    dispatchLocal(topic, data);

    // 2. UDP 广播发送
    if (udp_socket_fd_ >= 0) {
        // 协议: topic|data
        std::string raw_packet = topic + "|" + data;
        
        sendto(udp_socket_fd_, raw_packet.c_str(), raw_packet.size(), 0, 
               (struct sockaddr*)&broadcast_addr_, sizeof(broadcast_addr_));
    }

    return true;
}

int64_t PubSubMiddleware::subscribe(const std::string& topic, SubscribeCallback callback) {
    if (topic.empty() || !callback) return -1;
    
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t subscribe_id = next_subscribe_id_++;
    
    Subscription sub;
    sub.id = subscribe_id;
    sub.topic = topic;
    sub.callback = callback;

    subscriptions_[subscribe_id] = sub;
    topic_subscribers_[topic].push_back(subscribe_id);

    return subscribe_id;
}

bool PubSubMiddleware::unsubscribe(int64_t subscribe_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto sub_it = subscriptions_.find(subscribe_id);
    if (sub_it == subscriptions_.end()) return false;

    std::string topic = sub_it->second.topic;
    auto topic_it = topic_subscribers_.find(topic);
    if (topic_it != topic_subscribers_.end()) {
        auto& ids = topic_it->second;
        ids.erase(std::remove(ids.begin(), ids.end(), subscribe_id), ids.end());
        
        if (ids.empty()) {
            topic_subscribers_.erase(topic_it);
        }
    }

    subscriptions_.erase(sub_it);
    return true;
}

size_t PubSubMiddleware::unsubscribeTopic(const std::string& topic) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = topic_subscribers_.find(topic);
    if (it == topic_subscribers_.end()) return 0;

    size_t count = it->second.size();
    for (int64_t sub_id : it->second) {
        subscriptions_.erase(sub_id);
    }
    topic_subscribers_.erase(it);
    return count;
}

size_t PubSubMiddleware::getSubscriberCount(const std::string& topic) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = topic_subscribers_.find(topic);
    if (it == topic_subscribers_.end()) return 0;
    return it->second.size();
}

std::vector<std::string> PubSubMiddleware::getAllTopics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> topics;
    for (const auto& pair : topic_subscribers_) {
        topics.push_back(pair.first);
    }
    return topics;
}

}  // namespace simple_middleware
