#include "sensor_component.hpp"
#include <iostream>
#include <cmath>
#include <chrono>
#include <cstdlib> // for rand()
#include <fstream>
#include <vector>
#include <simple_middleware/logger.hpp> // Add logger include

using namespace simple_middleware;

SensorComponent::SensorComponent() 
    : running_(false), noise_distribution_(0.0f, 0.2f) { // 噪声标准差 0.2m
    status_reporter_ = std::make_unique<StatusReporter>("SensorNode");
}

SensorComponent::~SensorComponent() {
    Stop();
}

void SensorComponent::Start() {
    if (running_) return;
    running_ = true;

    // 订阅真值数据
    auto& middleware = PubSubMiddleware::getInstance();
    middleware.subscribe("visualizer/data", [this](const Message& msg) {
        this->OnVisualizerData(msg);
    });

    thread_ = std::thread(&SensorComponent::RunLoop, this);
    status_reporter_->Start();
    
    // 加载静态测试图片
    // 注意：假设运行目录是项目根目录 simple-autopilot/
    std::ifstream file("assets/test_image.ppm", std::ios::binary | std::ios::ate);
    if (file) {
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        raw_image_buffer_.resize(size);
        if (file.read(raw_image_buffer_.data(), size)) {
            simple_middleware::Logger::Info("Loaded test image: " + std::to_string(size) + " bytes.");
        }
    } else {
        // 尝试另一种常见路径 (如果在 bin 目录下运行)
        file.open("../assets/test_image.ppm", std::ios::binary | std::ios::ate);
        if (file) {
             std::streamsize size = file.tellg();
             file.seekg(0, std::ios::beg);
             raw_image_buffer_.resize(size);
             if (file.read(raw_image_buffer_.data(), size)) {
                 simple_middleware::Logger::Info("Loaded test image (fallback): " + std::to_string(size) + " bytes.");
             }
        } else {
             simple_middleware::Logger::Warn("Failed to load assets/test_image.ppm. Using random noise.");
        }
    }

    simple_middleware::Logger::Info("Started camera simulation.");
}

void SensorComponent::Stop() {
    status_reporter_->Stop();
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void SensorComponent::OnVisualizerData(const Message& msg) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (ground_truth_.ParseFromString(msg.data)) {
        has_ground_truth_ = true;
    }
}

void SensorComponent::RunLoop() {
    auto& middleware = PubSubMiddleware::getInstance();
    
    while (running_) {
        auto start_time = std::chrono::steady_clock::now();

        // 1. 获取最新真值
        senseauto::demo::FrameData current_gt;
        bool has_data = false;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            if (has_ground_truth_) {
                current_gt = ground_truth_;
                has_data = true;
            }
        }

        if (has_data && current_gt.has_car_state()) {
            senseauto::demo::CameraFrame camera_frame;
            camera_frame.set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            
            // 获取本车位姿 (世界坐标系)
            float ego_x = current_gt.car_state().position().x();
            float ego_y = current_gt.car_state().position().y();
            float ego_heading = current_gt.car_state().heading();

            // 遍历所有障碍物进行坐标转换
            for (const auto& obs : current_gt.obstacles()) {
                // 1. 计算相对坐标 (World -> Ego)
                float dx = obs.position().x() - ego_x;
                float dy = obs.position().y() - ego_y;
                
                // 旋转矩阵 (逆时针旋转 -heading)
                // rel_x 是车头方向 (纵向), rel_y 是左侧方向 (横向)
                float rel_x = dx * std::cos(-ego_heading) - dy * std::sin(-ego_heading);
                float rel_y = dx * std::sin(-ego_heading) + dy * std::cos(-ego_heading);

                // 2. 转换到相机坐标系 (Ego -> Camera)
                // 假设相机安装在 x 轴正方向 config_.pos_x 处
                float cam_x = rel_x - config_.pos_x;
                float cam_y = rel_y - config_.pos_y;

                // 3. 视场角过滤 (FOV)
                // 计算目标在相机坐标系下的角度
                float angle = std::atan2(cam_y, cam_x) * 180.0f / M_PI;
                float dist = std::sqrt(cam_x * cam_x + cam_y * cam_y);

                // 只保留前方且在 FOV 内的物体
                if (cam_x > 0 && std::abs(angle) < (config_.fov / 2.0f) && dist < config_.max_distance) {
                    auto* cam_obj = camera_frame.add_objects();
                    cam_obj->set_id(obs.id());
                    cam_obj->set_type(obs.type());
                    
                    // 4. 添加测量噪声
                    float noise_x = noise_distribution_(generator_);
                    float noise_y = noise_distribution_(generator_);
                    
                    cam_obj->set_rel_x(cam_x + noise_x);
                    cam_obj->set_rel_y(cam_y + noise_y);
                    
                    // 简单假设宽高
                    cam_obj->set_width(1.8f);
                    cam_obj->set_height(1.6f);
                }
            }

            // 【模拟生成图像数据】
            // 如果成功加载了图片，则发送；否则发送原来的噪点图
            if (!raw_image_buffer_.empty()) {
                camera_frame.set_image_width(160);
                camera_frame.set_image_height(120);
                camera_frame.set_image_format("ppm"); // 标记为 ppm 格式
                camera_frame.set_raw_image(raw_image_buffer_);
            } else {
                // 原来的噪点图生成逻辑 (fallback)
                int width = 320;
                int height = 240;
                std::string raw_image_data;
                raw_image_data.resize(width * height);
                for (int i = 0; i < width * height; ++i) {
                    raw_image_data[i] = static_cast<char>(std::rand() % 256);
                }
                camera_frame.set_image_width(width);
                camera_frame.set_image_height(height);
                camera_frame.set_image_format("raw_gray");
                camera_frame.set_raw_image(raw_image_data);
            }

            // 发布传感器数据
            std::string serialized_data;
            if (camera_frame.SerializeToString(&serialized_data)) {
                middleware.publish("sensor/camera/front", serialized_data);
                
                // Debug log
                static int log_counter = 0;
                if (log_counter++ % 30 == 0) {
                    simple_middleware::Logger::Debug("Published frame. Image size: " + std::to_string(camera_frame.raw_image().size()));
                }
            }
        }

        // 30Hz 频率控制
        std::this_thread::sleep_until(start_time + std::chrono::milliseconds(33));
    }
}

