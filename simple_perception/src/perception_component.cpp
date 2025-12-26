#include "perception_component.hpp"
#include <iostream>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <json11.hpp>
#include <google/protobuf/util/json_util.h>
#include <common_msgs/sensor_data.pb.h> 
#include <simple_middleware/logger.hpp> // Add logger

using namespace json11;

PerceptionComponent::PerceptionComponent() : running_(false) {
    status_reporter_ = std::make_unique<simple_middleware::StatusReporter>("PerceptionNode");
}

PerceptionComponent::~PerceptionComponent() {
    Stop();
}

void PerceptionComponent::Start() {
    if (running_) return;
    running_ = true;
    
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    
    // 订阅车辆状态以获知自身位置（用于将相对坐标转为绝对坐标）
    middleware.subscribe("visualizer/data", [this](const simple_middleware::Message& msg) {
        this->OnCarStatus(msg);
    });

    // 订阅 Sensor 发来的相机数据
    middleware.subscribe("sensor/camera/front", [this](const simple_middleware::Message& msg) {
        this->OnCameraData(msg);
    });

    thread_ = std::thread(&PerceptionComponent::RunLoop, this);
    status_reporter_->Start();
    simple_middleware::Logger::Info("Started loop.");
}

void PerceptionComponent::Stop() {
    status_reporter_->Stop();
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void PerceptionComponent::RunLoop() {
    // Perception 现在的核心是 OnCameraData 回调，RunLoop 主要负责监控或定时任务
    // 这里我们保持简单的休眠即可
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void PerceptionComponent::OnCarStatus(const simple_middleware::Message& msg) {
    senseauto::demo::FrameData frame;
    if (frame.ParseFromString(msg.data)) {
         std::lock_guard<std::mutex> lock(state_mutex_);
         if (frame.has_car_state()) {
             current_car_state_ = frame.car_state();
         }
    }
}

void PerceptionComponent::OnCameraData(const simple_middleware::Message& msg) {
    senseauto::demo::CameraFrame frame;
    if (!frame.ParseFromString(msg.data)) return;

    // 将 Sensor 的相对坐标转换回世界坐标 (因为 Planning 需要世界坐标)
    // 这是一个简化的处理：通常 Sensor->Perception->Fusion->Planning
    // 这里 Perception 承担了 Fusion 的部分职责 (Frame Transformation)
    
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    // 1. 获取自车位姿
    double car_x = current_car_state_.position().x();
    double car_y = current_car_state_.position().y();
    double car_heading = current_car_state_.heading();

    // 2. 准备输出
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    Json::array obs_array;
    senseauto::demo::Detection2DArray det_array;
    det_array.set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    for (const auto& cam_obj : frame.objects()) {
        // A. 转换世界坐标 (用于 Planning)
        // Camera frame: x forward, y left
        // World frame transformation:
        // world_x = car_x + rel_x * cos(heading) - rel_y * sin(heading)
        // world_y = car_y + rel_x * sin(heading) + rel_y * cos(heading)
        
        double rel_x = cam_obj.rel_x();
        double rel_y = cam_obj.rel_y();
        
        double world_x = car_x + rel_x * std::cos(car_heading) - rel_y * std::sin(car_heading);
        double world_y = car_y + rel_x * std::sin(car_heading) + rel_y * std::cos(car_heading);

        obs_array.push_back(Json::object{
            {"id", cam_obj.id()},
            {"position", Json::object{
                {"x", world_x},
                {"y", world_y},
                {"z", 0.0}
            }},
            {"type", cam_obj.type()}
        });

        // B. 生成 2D Bounding Box (用于 Visualizer 显示)
        // 简单的投影模型：x = f * Y / X, y = f * Z / X (这里简化)
        // 假设图像中心是 (80, 60)，简单映射
        // 越远越小
        if (rel_x > 0.5) {
            auto* box = det_array.add_boxes();
            double scale = 100.0 / rel_x; // 透视缩放
            int w = static_cast<int>(cam_obj.width() * scale * 20); // 任意系数
            int h = static_cast<int>(cam_obj.height() * scale * 20);
            
            // 投影横向位置: rel_y 左正右负
            // 图像中心 x=80. rel_y=0 -> x=80. rel_y>0 -> x<80 (left)
            // 假设 FOV 60度 -> tan(30) = 0.577
            // x_img = 80 - (rel_y / (rel_x * 0.577)) * 80
            int cx = 80 - static_cast<int>((rel_y / (rel_x * 0.577)) * 80);
            int cy = 60 + static_cast<int>(5.0 / rel_x); // 稍微向下偏
            
            box->set_x(cx - w/2);
            box->set_y(cy - h/2);
            box->set_width(w);
            box->set_height(h);
            box->set_label(cam_obj.type());
            box->set_score(0.9);
        }
    }

    // 3. 发布 "perception/obstacles" (JSON, for Planning)
    Json json_payload = Json::object{
        {"type", "perception_obstacles"},
        {"obstacles", obs_array}
    };
    middleware.publish("perception/obstacles", json_payload.dump());

    // 4. 发布 "perception/detection_2d" (Proto, for Visualizer)
    std::string det_data;
    det_array.SerializeToString(&det_data);
    middleware.publish("perception/detection_2d", det_data);
}
