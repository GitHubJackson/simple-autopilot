#pragma once

#include <simple_middleware/pub_sub_middleware.hpp>
#include <simple_middleware/status_reporter.hpp>
#include <common_msgs/visualizer_data.pb.h>
// #include <common_msgs/control_command.pb.h> // Removed: defined in visualizer_data.pb.h
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>

class SimulatorCore {
public:
    SimulatorCore();
    ~SimulatorCore();

    void Start();
    void Stop();

private:
    void RunLoop();
    void OnControlCommand(const simple_middleware::Message& msg);
    void OnControlMessage(const simple_middleware::Message& msg); // 处理 reset 等命令
    
    // 物理步进
    void StepPhysics(double dt);
    
    // 初始化模拟场景
    void InitScenario();

private:
    std::atomic<bool> running_;
    std::thread thread_;
    std::mutex state_mutex_;
    std::unique_ptr<simple_middleware::StatusReporter> status_reporter_;

    // 世界状态
    senseauto::demo::FrameData world_state_;
    double time_accumulator_ = 0.0;

    // 接收到的控制量 (来自 Control 模块)
    double target_speed_ = 0.0; // 模拟油门/刹车结果
    double target_steering_ = 0.0;
    
    // 发布节流：物理仿真保持 100Hz，但只每 N 帧发布一次给前端
    int publish_counter_ = 0;
    const int PUBLISH_INTERVAL = 5; // 每 5 帧发布一次，即 20Hz (100Hz / 5)
    
    // 车辆物理参数
    const double WHEELBASE = 2.8;
};

