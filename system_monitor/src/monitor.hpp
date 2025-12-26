#pragma once

#include <simple_middleware/pub_sub_middleware.hpp>
#include <common_msgs/daemon.pb.h>
#include <chrono>
#include <string>
#include <map>
#include <mutex>
#include <vector>
#include <thread>
#include <atomic>

enum class MonitorMode {
    ALL,
    NODE_STATUS,
    TOPIC_STATUS
};

// 流量统计信息
struct TopicStats {
    uint64_t count = 0;
    uint64_t bytes = 0;
    std::chrono::system_clock::time_point last_msg_time;
    
    // 频率计算 (1秒窗口)
    uint64_t msgs_in_window = 0;
    std::chrono::system_clock::time_point window_start;
    float current_hz = 0.0f;
};

// 节点状态信息
struct NodeStatusInfo {
    simple_daemon::SystemStatus::NodeStatus status;
    std::chrono::system_clock::time_point last_seen;
};

// 车辆数据
struct VehicleData {
    bool has_data = false;
    uint64_t frame_id = 0;
    float battery = 0.0f;
    int obstacle_count = 0;
    float speed = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    int trajectory_points = 0;
};

class SystemMonitor {
public:
    SystemMonitor();
    ~SystemMonitor();

    void Init();
    void Run(MonitorMode mode);

private:
    void OnMessage(const simple_middleware::Message& msg);
    void PrintStats(MonitorMode mode);
    std::string StateToString(bool is_running);

    // 成员变量
    std::mutex mutex_;
    std::map<std::string, TopicStats> topic_stats_;
    std::map<std::string, NodeStatusInfo> node_stats_;
    VehicleData vehicle_data_;
    
    std::atomic<bool> running_;
};
