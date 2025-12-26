#pragma once

#include <common_msgs/visualizer_data.pb.h>
#include <simple_middleware/pub_sub_middleware.hpp>
#include <simple_middleware/status_reporter.hpp>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <memory>
#include <vector>

class ControlComponent {
public:
    ControlComponent();
    ~ControlComponent();

    void Start();
    void Stop();

    // 控制接口
    void SetSpeed(double speed);
    void SetSteering(double angle);
    void Reset();
    void SetTarget(double x, double y); // 设置追踪目标点 (废弃，优先使用轨迹)

private:
    void RunLoop();
    void OnControlMessage(const simple_middleware::Message& msg);
    void OnPlanningTrajectory(const simple_middleware::Message& msg);
    void OnSimulatorState(const simple_middleware::Message& msg); // New
    
    // 纯追踪算法 (Pure Pursuit)
    void ComputePurePursuitSteering(double dt);
    
    // 轨迹跟踪辅助
    void UpdateLookaheadPoint();

private:
    std::atomic<bool> running_;
    std::thread thread_;
    std::mutex state_mutex_;

    // 车辆状态 (从 Simulator 同步)
    senseauto::demo::CarState current_car_state_;
    
    // 纯追踪参数
    struct TargetPoint {
        double x = 0.0;
        double y = 0.0;
        bool active = false;
    };
    TargetPoint target_point_;

    // 接收到的规划轨迹
    std::vector<std::pair<double, double>> current_trajectory_;
    
    // Control Parameters (loaded from config)
    double wheelbase_ = 2.8;
    double max_steer_ = 0.5;
    double kp_ = 1.0;
    double lookahead_dist_ = 2.0;
    double max_speed_ = 30.0;
    double auto_engage_speed_ = 5.0;

    std::unique_ptr<simple_middleware::StatusReporter> status_reporter_;
};
