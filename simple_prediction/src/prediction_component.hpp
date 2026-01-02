#pragma once

#include <memory>
#include <thread>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <simple_middleware/pub_sub_middleware.hpp>
#include <simple_middleware/status_reporter.hpp>
#include <common_msgs/visualizer_data.pb.h>
#include "json11.hpp"

// 障碍物历史状态（用于计算速度）
struct ObstacleHistory {
    double x = 0.0;
    double y = 0.0;
    double heading = 0.0;
    int64_t timestamp = 0;  // 毫秒时间戳
    
    // 计算得到的速度（m/s）
    double vx = 0.0;  // x方向速度
    double vy = 0.0;  // y方向速度
    double speed = 0.0;  // 总速度
};

// 预测轨迹点
struct PredictedPoint {
    double x;
    double y;
    double confidence;  // 预测置信度（0-1）
    double time_offset;  // 相对当前时间的时间偏移（秒）
};

class PredictionComponent {
public:
    PredictionComponent();
    ~PredictionComponent();

    void Start();
    void Stop();

private:
    void RunLoop();
    void OnPerceptionObstacles(const simple_middleware::Message& msg);
    void OnCarStatus(const simple_middleware::Message& msg);
    
    // 预测障碍物未来轨迹（匀速模型）
    std::vector<PredictedPoint> PredictObstacleTrajectory(
        const ObstacleHistory& history, 
        double prediction_horizon = 5.0,  // 预测时间范围（秒）
        double time_step = 0.1  // 时间步长（秒）
    );
    
    // 更新障碍物历史状态并计算速度
    void UpdateObstacleHistory(int32_t obstacle_id, double x, double y, double heading, int64_t timestamp);

private:
    std::atomic<bool> running_;
    std::thread thread_;
    std::unique_ptr<simple_middleware::StatusReporter> status_reporter_;
    
    std::mutex state_mutex_;
    
    // 自车状态（用于坐标转换）
    struct {
        double x = 0.0;
        double y = 0.0;
        double heading = 0.0;
    } ego_state_;
    
    // 障碍物历史状态（obstacle_id -> history）
    std::unordered_map<int32_t, ObstacleHistory> obstacle_histories_;
    
    // 配置参数
    double prediction_horizon_ = 5.0;  // 预测时间范围（秒）
    double time_step_ = 0.1;  // 预测时间步长（秒）
    double min_speed_threshold_ = 0.1;  // 最小速度阈值（m/s），低于此值视为静止
};

