#include "monitor.hpp"
#include <common_msgs/visualizer_data.pb.h>
#include <iostream>
#include <iomanip>
#include <thread>

using namespace simple_middleware;

SystemMonitor::SystemMonitor() : running_(false) {}

SystemMonitor::~SystemMonitor() {
    running_ = false;
}

void SystemMonitor::Init() {
    auto& middleware = PubSubMiddleware::getInstance();
    
    // 使用 lambda 绑定成员函数
    auto callback = [this](const Message& msg) {
        this->OnMessage(msg);
    };

    // 订阅业务主题
    middleware.subscribe("visualizer/data", callback);
    middleware.subscribe("visualizer/control", callback);
    middleware.subscribe("planning/trajectory", callback);
    
    // 订阅系统状态 (来自 Daemon)
    middleware.subscribe("system/status", callback);
}

void SystemMonitor::Run(MonitorMode mode) {
    running_ = true;
    PrintStats(mode);
}

void SystemMonitor::OnMessage(const Message& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::system_clock::now();
    
    // 更新主题流量统计
    auto& stat = topic_stats_[msg.topic];
    stat.count++;
    stat.bytes += msg.data.size();
    stat.last_msg_time = now;
    
    // Hz 计算
    if (stat.window_start.time_since_epoch().count() == 0) {
        stat.window_start = now;
    }
    stat.msgs_in_window++;
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - stat.window_start).count();
    if (duration >= 1000) {
        stat.current_hz = (float)stat.msgs_in_window / (duration / 1000.0f);
        stat.msgs_in_window = 0;
        stat.window_start = now;
    }

    if (msg.topic == "system/status") {
        simple_daemon::SystemStatus sys_status;
        if (sys_status.ParseFromString(msg.data)) {
            for (const auto& node : sys_status.nodes()) {
                node_stats_[node.name()] = NodeStatusInfo{node, std::chrono::system_clock::now()};
            }
        }
    }
    // 解析车辆数据
    else if (msg.topic == "visualizer/data") {
        senseauto::demo::FrameData frame;
        if (frame.ParseFromString(msg.data)) {
            vehicle_data_.has_data = true;
            vehicle_data_.frame_id = frame.frame_id();
            vehicle_data_.battery = frame.battery_level();
            vehicle_data_.obstacle_count = frame.obstacles_size();
            
            if (frame.has_car_state()) {
                vehicle_data_.speed = frame.car_state().speed();
                if (frame.car_state().has_position()) {
                    vehicle_data_.x = frame.car_state().position().x();
                    vehicle_data_.y = frame.car_state().position().y();
                }
            }
        }
    }
    else if (msg.topic == "planning/trajectory") {
        senseauto::demo::FrameData traj;
        if (traj.ParseFromString(msg.data)) {
            vehicle_data_.trajectory_points = traj.trajectory_size();
        }
    }
}

std::string SystemMonitor::StateToString(bool is_running) {
    return is_running ? "\033[32mRUNNING\033[0m" : "\033[31mSTOPPED\033[0m";
}

void SystemMonitor::PrintStats(MonitorMode mode) {
    while (running_) {
        // 清屏
        std::cout << "\033[2J\033[1;1H";
        
        std::cout << "=== SenseAuto System Monitor ===" << std::endl;
        std::cout << "Time: " << std::time(nullptr) << std::endl;
        std::cout << "----------------------------------------------------------------" << std::endl;
        
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();

        // 1. 车辆仪表盘 (Business Metrics)
        if (mode == MonitorMode::ALL) {
            std::cout << ">>> Vehicle Dashboard" << std::endl;
            if (vehicle_data_.has_data) {
                std::cout << "Speed:    " << std::fixed << std::setprecision(1) << vehicle_data_.speed << " m/s" 
                          << "   Battery: " << std::setprecision(0) << vehicle_data_.battery << "%" << std::endl;
                std::cout << "Position: (" << std::setprecision(1) << vehicle_data_.x << ", " << vehicle_data_.y << ")"
                          << "   Obstacles: " << vehicle_data_.obstacle_count << std::endl;
                std::cout << "Frame ID: " << vehicle_data_.frame_id 
                          << "      Plan Pts:  " << vehicle_data_.trajectory_points << std::endl;
            } else {
                std::cout << "(Waiting for vehicle data...)" << std::endl;
            }
            std::cout << std::endl;
        }

        // 2. 节点状态面板 (Daemon Status)
        if (mode == MonitorMode::ALL || mode == MonitorMode::NODE_STATUS) {
            std::cout << ">>> Node Status (Reported by Daemon)" << std::endl;
            std::cout << std::left << std::setw(20) << "NODE" 
                      << std::setw(15) << "STATE" 
                      << std::setw(10) << "PID" 
                      << std::setw(10) << "%CPU"
                      << std::setw(10) << "MEM(MB)"
                      << std::setw(10) << "LAST SEEN" << std::endl;
            
            if (node_stats_.empty()) {
                std::cout << "(No daemon status received)" << std::endl;
            } else {
                for (const auto& pair : node_stats_) {
                    const auto& node = pair.second.status;
                    const auto& last_seen = pair.second.last_seen;
                    
                    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - last_seen).count();
                    
                    // 判定 Daemon 是否断连
                    bool timeout = duration_ms > 5000;
                    std::string state_str = timeout ? "\033[33mSTALE\033[0m" : StateToString(node.is_running());
                    std::string time_str = std::to_string(duration_ms) + "ms";

                    std::cout << std::left << std::setw(20) << node.name() 
                              << std::setw(24) << state_str // width includes escape codes
                              << std::setw(10) << node.pid()
                              << std::setw(10) << std::fixed << std::setprecision(1) << node.cpu_usage()
                              << std::setw(10) << std::fixed << std::setprecision(1) << node.memory_usage()
                              << std::setw(10) << time_str << std::endl;
                }
            }
            std::cout << std::endl;
        }

        // 3. 网络流量面板 (Topic Status)
        if (mode == MonitorMode::ALL || mode == MonitorMode::TOPIC_STATUS) {
            std::cout << ">>> Network Traffic & Diagnostics" << std::endl;
            std::cout << std::left << std::setw(25) << "TOPIC" 
                      << std::setw(10) << "HZ" 
                      << std::setw(10) << "MSGS" 
                      << std::setw(10) << "BYTES" 
                      << "STATUS" << std::endl;
                      
            for (const auto& pair : topic_stats_) {
                const auto& topic = pair.first;
                const auto& stat = pair.second;
                
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - stat.last_msg_time).count();
                
                std::string status = (duration < 1000) ? "ACTIVE" : "IDLE";
                if (duration > 5000) status = "OFFLINE";
                
                // 简单的 Hz 诊断告警
                if (topic == "visualizer/data" && status == "ACTIVE" && stat.current_hz < 5.0f) {
                    status = "\033[33mLOW FPS\033[0m"; // Yellow Warning
                }

                std::cout << std::left << std::setw(25) << topic 
                          << std::setw(10) << std::fixed << std::setprecision(1) << stat.current_hz
                          << std::setw(10) << stat.count 
                          << std::setw(10) << stat.bytes 
                          << status << std::endl;
            }
        }
        
        std::cout << "----------------------------------------------------------------" << std::endl;
        std::cout << "Press Ctrl+C to exit." << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
