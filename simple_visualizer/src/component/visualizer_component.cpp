#include "visualizer_component.hpp"
#include <json11.hpp>
#include <iostream>
#include <chrono>
#include <ctime>
#include <cmath>
#include <cstring> // for memcpy

using namespace json11;

VisualizerComponent::VisualizerComponent() {
    Reset();
}

void VisualizerComponent::Reset() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    frame_data_.Clear();
    
    // 初始化一个默认状态
    auto* car = frame_data_.mutable_car_state();
    car->mutable_position()->set_x(0.0);
    car->mutable_position()->set_y(0.0);
    car->set_heading(0.0);
    car->set_speed(0.0);
    car->set_steering_angle(0.0);

    time_accumulator_ = 0.0;
}

// Visualizer 不再负责设置 Speed/Steering，它只是被动接收
// 但为了兼容旧接口，先留空或者保留打印
void VisualizerComponent::SetSpeed(double speed) {
    // Deprecated
}

void VisualizerComponent::SetSteering(double angle) {
    // Deprecated
}

// 核心更新逻辑：从 Simulator 同步状态
void VisualizerComponent::UpdateFromSimulator(const senseauto::demo::FrameData& sim_frame) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    // 直接覆盖本地状态
    frame_data_ = sim_frame;
}

void VisualizerComponent::Update(double dt) {
    // Visualizer 不再做物理积分
    // 这里的 Update 可以用来做一些纯视觉效果的动画（比如车轮转动动画），如果需要的话
    time_accumulator_ += dt;
}

void VisualizerComponent::UpdateCameraImage(const std::string& ppm_data) {
    std::lock_guard<std::mutex> lock(img_mutex_);
    if (current_image_.FromBuffer(ppm_data)) {
        has_new_image_ = true;
    }
}

void VisualizerComponent::UpdateDetections(const senseauto::demo::Detection2DArray& dets) {
    std::lock_guard<std::mutex> lock(img_mutex_);
    current_detections_ = dets;
}

std::vector<unsigned char> VisualizerComponent::GetRenderedImage() {
    std::lock_guard<std::mutex> lock(img_mutex_);
    if (!has_new_image_ || current_image_.width == 0) return {};

    // 绘制 2D 检测框
    for (const auto& box : current_detections_.boxes()) {
        simple_image::Pixel red = {255, 0, 0};
        current_image_.DrawRect(box.x(), box.y(), box.width(), box.height(), red, 2);
    }
    
    // 返回 Raw RGB Buffer (去除 PPM Header)
    // 格式: [Width:4][Height:4][RGB Data]
    
    int w = current_image_.width;
    int h = current_image_.height;
    size_t size = w * h * 3;
    
    std::vector<unsigned char> result;
    result.resize(8 + size); // 4 byte width + 4 byte height + data
    
    // Write Width (Big Endian)
    result[0] = (w >> 24) & 0xFF;
    result[1] = (w >> 16) & 0xFF;
    result[2] = (w >> 8) & 0xFF;
    result[3] = w & 0xFF;
    
    // Write Height (Big Endian)
    result[4] = (h >> 24) & 0xFF;
    result[5] = (h >> 16) & 0xFF;
    result[6] = (h >> 8) & 0xFF;
    result[7] = h & 0xFF;
    
    std::memcpy(result.data() + 8, current_image_.data.data(), size);
    
    return result;
}

// 序列化给前端 WebSocket
std::string VisualizerComponent::GetSerializedData(int frame_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    // 1. 处理障碍物列表
    Json::array obstacles_json;
    for (int i = 0; i < frame_data_.obstacles_size(); ++i) {
        const auto& obs = frame_data_.obstacles(i);
        obstacles_json.push_back(Json::object {
            {"id", obs.id()},
            {"type", obs.type()},
            {"position", Json::object {
                {"x", obs.position().x()},
                {"y", obs.position().y()}
            }},
            // 将新增字段也传给前端（前端目前可能还没用，但以后会有用）
            {"length", obs.length()},
            {"width", obs.width()},
            {"heading", obs.heading()}
        });
    }

    // 2. 构造最终的 JSON 对象
    Json final_json = Json::object {
        {"type", "frame_data"},
        {"frame_id", frame_id},
        {"timestamp", (int)std::time(nullptr)},
        {"car_state", Json::object {
            {"speed", frame_data_.car_state().speed()},
            {"heading", frame_data_.car_state().heading()},
            {"position", Json::object {
                {"x", frame_data_.car_state().position().x()},
                {"y", frame_data_.car_state().position().y()}
            }}
        }},
        {"obstacles", obstacles_json}
    };

    return final_json.dump();
}
