#pragma once

#include <memory>
#include <thread>
#include <mutex>
#include <vector>
#include "pub_sub_middleware.hpp"
#include "status_reporter.hpp"
#include <common_msgs/visualizer_data.pb.h>
#include <common_msgs/sensor_data.pb.h> // 新增

class PerceptionComponent {
public:
    PerceptionComponent();
    ~PerceptionComponent();

    void Start();
    void Stop();

private:
    void RunLoop();
    void OnCarStatus(const simple_middleware::Message& msg);
    void OnCameraData(const simple_middleware::Message& msg);
    
    bool running_;
    std::thread thread_;
    std::unique_ptr<simple_middleware::StatusReporter> status_reporter_;
    
    std::mutex state_mutex_;
    senseauto::demo::CarState current_car_state_;
    senseauto::demo::FrameData current_ground_truth_; // 用于模拟检测算法
    bool has_ground_truth_ = false;
    std::vector<senseauto::demo::Obstacle> obstacles_;
    
    double time_accumulator_ = 0.0;
};

