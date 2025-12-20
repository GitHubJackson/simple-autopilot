/*
 * @Desc: 中间件流量与节点状态监视器
 * @Author: Assistant
 * @Date: 2025/12/20
 */

#include <simple_middleware/pub_sub_middleware.hpp>
#include <common_msgs/system_status.pb.h>
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <map>
#include <mutex>
#include <ctime>

using namespace simple_middleware;

// 流量统计信息
struct TopicStats {
    uint64_t count = 0;
    uint64_t bytes = 0;
    std::chrono::system_clock::time_point last_msg_time;
};

// 节点状态缓存
struct NodeInfo {
    senseauto::demo::NodeStatus status;
    std::chrono::system_clock::time_point last_seen;
};

std::map<std::string, TopicStats> g_topic_stats;
std::map<std::string, NodeInfo> g_node_stats;
std::mutex g_mutex;

void OnMessage(const Message& msg) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    // 更新主题流量统计
    auto& stat = g_topic_stats[msg.topic];
    stat.count++;
    stat.bytes += msg.data.size();
    stat.last_msg_time = std::chrono::system_clock::now();

    // 如果是节点状态消息，解析并更新节点列表
    if (msg.topic == "system/node_status") {
        senseauto::demo::NodeStatus status;
        if (status.ParseFromString(msg.data)) {
            g_node_stats[status.node_name()] = {status, std::chrono::system_clock::now()};
        }
    }
}

std::string StateToString(senseauto::demo::NodeStatus::State state) {
    switch(state) {
        case senseauto::demo::NodeStatus::OK: return "\033[32mOK\033[0m"; // Green
        case senseauto::demo::NodeStatus::WARN: return "\033[33mWARN\033[0m"; // Yellow
        case senseauto::demo::NodeStatus::ERROR: return "\033[31mERROR\033[0m"; // Red
        case senseauto::demo::NodeStatus::OFFLINE: return "\033[37mOFFLINE\033[0m"; // Gray
        default: return "UNKNOWN";
    }
}

void PrintStats() {
    while (true) {
        // 清屏
        std::cout << "\033[2J\033[1;1H";
        
        std::cout << "=== SenseAuto System Monitor ===" << std::endl;
        std::cout << "Time: " << std::time(nullptr) << std::endl;
        std::cout << "----------------------------------------------------------------" << std::endl;
        
        std::lock_guard<std::mutex> lock(g_mutex);
        auto now = std::chrono::system_clock::now();

        // 1. 节点状态面板
        std::cout << ">>> Node Status" << std::endl;
        std::cout << std::left << std::setw(20) << "NODE" 
                  << std::setw(10) << "STATE" 
                  << std::setw(10) << "LAST SEEN" 
                  << "MESSAGE" << std::endl;
        
        if (g_node_stats.empty()) {
            std::cout << "(No nodes detected yet)" << std::endl;
        } else {
            for (const auto& pair : g_node_stats) {
                const auto& node = pair.second.status;
                const auto& last_seen = pair.second.last_seen;
                
                auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_seen).count();
                
                // 判定是否超时
                bool timeout = duration_ms > 3000;
                std::string state_str = timeout ? "\033[31mTIMEOUT\033[0m" : StateToString(node.state());
                std::string time_str = std::to_string(duration_ms) + "ms";

                std::cout << std::left << std::setw(20) << node.node_name() 
                          << std::setw(20) << state_str // width includes escape codes
                          << std::setw(10) << time_str 
                          << node.message() << std::endl;
            }
        }
        std::cout << std::endl;

        // 2. 网络流量面板
        std::cout << ">>> Network Traffic" << std::endl;
        std::cout << std::left << std::setw(25) << "TOPIC" 
                  << std::setw(10) << "MSGS" 
                  << std::setw(10) << "BYTES" 
                  << "STATUS" << std::endl;
                  
        for (const auto& pair : g_topic_stats) {
            const auto& topic = pair.first;
            const auto& stat = pair.second;
            
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - stat.last_msg_time).count();
            
            std::string status = (duration < 1000) ? "ACTIVE" : "IDLE";
            if (duration > 5000) status = "OFFLINE";

            std::cout << std::left << std::setw(25) << topic 
                      << std::setw(10) << stat.count 
                      << std::setw(10) << stat.bytes 
                      << status << std::endl;
        }
        
        std::cout << "----------------------------------------------------------------" << std::endl;
        std::cout << "Press Ctrl+C to exit." << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main() {
    auto& middleware = PubSubMiddleware::getInstance();
    
    // 订阅业务主题
    middleware.subscribe("visualizer/data", OnMessage);
    middleware.subscribe("visualizer/control", OnMessage);
    
    // 订阅系统状态
    middleware.subscribe("system/node_status", OnMessage);
    
    // 启动显示线程
    std::thread printer(PrintStats);
    printer.join();
    
    return 0;
}

