#pragma once

#include <simple_middleware/pub_sub_middleware.hpp>
#include <simple_middleware/status_reporter.hpp>
#include <common_msgs/visualizer_data.pb.h>
#include <common_msgs/sensor_data.pb.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <random>

class SensorComponent {
public:
    SensorComponent();
    ~SensorComponent();

    void Start();
    void Stop();

private:
    void RunLoop();
    void OnVisualizerData(const simple_middleware::Message& msg);
    
    // 模拟相机参数
    struct CameraConfig {
        float fov = 60.0f;          // 视场角 (degrees)
        float max_distance = 80.0f; // 最大探测距离 (m)
        float pos_x = 2.0f;         // 安装位置相对于车辆中心的纵向偏移 (m)
        float pos_y = 0.0f;         // 横向偏移 (m)
    };

    // 存储最新的真值数据
    senseauto::demo::FrameData ground_truth_;
    bool has_ground_truth_ = false;
    std::mutex data_mutex_;

    std::unique_ptr<simple_middleware::StatusReporter> status_reporter_;
    std::thread thread_;
    std::atomic<bool> running_;
    
    // 随机数生成器用于模拟噪声
    std::default_random_engine generator_;
    std::normal_distribution<float> noise_distribution_;
    
    CameraConfig config_;
    std::string raw_image_buffer_; // 缓存加载的图片数据
};

