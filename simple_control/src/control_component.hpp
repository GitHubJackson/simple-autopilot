#pragma once

#include <common_msgs/visualizer_data.pb.h>
#include <simple_middleware/pub_sub_middleware.hpp>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>

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
    void SetTarget(double x, double y); // 设置追踪目标点

private:
    void RunLoop();
    void OnControlMessage(const simple_middleware::Message& msg);
    std::string GetSerializedData(int frame_id);
    
    // 纯追踪算法 (Pure Pursuit)
    // 根据当前位置和朝向，计算追踪目标点所需的转向角
    void ComputePurePursuitSteering(double dt);

    // JSON 解析辅助
    std::string parseJsonString(const std::string& json, const std::string& key);
    double parseJsonDouble(const std::string& json, const std::string& key);

private:
    std::atomic<bool> running_;
    std::thread thread_;
    std::mutex state_mutex_;

    // 车辆状态
    senseauto::demo::FrameData frame_data_;
    double time_accumulator_ = 0.0;
    
    // 纯追踪参数
    struct {
        double x = 0.0;
        double y = 0.0;
        bool active = false;
    } target_point_;
    
    const double WHEELBASE = 2.8; // 轴距
    const double MAX_STEER = 0.5; // 最大转角 (rad)
    const double KP = 1.0;        // P 控制器增益
