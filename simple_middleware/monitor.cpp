/*
 * @Desc: 中间件流量监视器
 * @Author: JacksonZhou
 * @Date: 2025/12/19
 * 
 * 功能：
 * 作为一个独立的节点加入网络，监听并打印所有流经中间件的消息。
 * 这可以帮助验证“中间件”确实在工作，数据是通过网络层传输的。
 */

#include "pub_sub_middleware.hpp"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

using namespace simple_middleware;

// 统计信息
struct TopicStats {
    uint64_t count = 0;
    uint64_t bytes = 0;
    std::chrono::system_clock::time_point last_msg_time;
};

std::unordered_map<std::string, TopicStats> g_stats;
std::mutex g_mutex;

void OnMessage(const Message& msg) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto& stat = g_stats[msg.topic];
    stat.count++;
    stat.bytes += msg.data.size();
    stat.last_msg_time = std::chrono::system_clock::now();
    
    // 实时打印简单的日志
    // std::cout << "[Monitor] Recv: " << msg.topic << " (" << msg.data.size() << " bytes)" << std::endl;
}

void PrintStats() {
    while (true) {
        // 清屏 (ANSI escape code)
        std::cout << "\033[2J\033[1;1H";
        
        std::cout << "=== SenseAuto Middleware Monitor ===" << std::endl;
        std::cout << "Listening on UDP Port: 12345" << std::endl;
        std::cout << "------------------------------------------------" << std::endl;
        std::cout << std::left << std::setw(25) << "TOPIC" 
                  << std::setw(10) << "MSGS" 
                  << std::setw(10) << "BYTES" 
                  << "STATUS" << std::endl;
        std::cout << "------------------------------------------------" << std::endl;

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto now = std::chrono::system_clock::now();
            
            for (const auto& pair : g_stats) {
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
        }
        std::cout << "------------------------------------------------" << std::endl;
        std::cout << "Press Ctrl+C to exit." << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main() {
    auto& middleware = PubSubMiddleware::getInstance();
    
    // 监听我们关心的主题
    // 注意：在真实的中间件(如ROS/DDS)中，通常有"嗅探"模式可以自动发现所有主题
    // 这里简易版我们需要手动指定监听
    middleware.subscribe("visualizer/data", OnMessage);
    middleware.subscribe("visualizer/control", OnMessage);
    
    // 启动显示线程
    std::thread printer(PrintStats);
    printer.join();
    
    return 0;
}
