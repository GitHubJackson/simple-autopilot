#include "control_component.hpp"
#include <simple_middleware/logger.hpp> // Add logger include
#include <google/protobuf/util/json_util.h>
#include <json11.hpp>
#include <simple_middleware/config_manager.hpp>
#include <iostream>
#include <chrono>
#include <cmath>
#include <algorithm>

using namespace json11;

ControlComponent::ControlComponent() : running_(false) {
    status_reporter_ = std::make_unique<simple_middleware::StatusReporter>("ControlNode");
    
    // Load config
    auto& config = simple_middleware::ConfigManager::GetInstance();
    if (config.Load("control", "config/control.json")) {
        wheelbase_ = config.Get<double>("control", "wheelbase", 2.8);
        max_steer_ = config.Get<double>("control", "max_steer", 0.5);
        kp_ = config.Get<double>("control", "kp", 1.0);
        lookahead_dist_ = config.Get<double>("control", "lookahead_dist", 2.0);
        max_speed_ = config.Get<double>("control", "max_speed", 30.0);
        auto_engage_speed_ = config.Get<double>("control", "auto_engage_speed", 5.0);
        simple_middleware::Logger::Info("Config loaded. Max Speed: " + std::to_string(max_speed_));
    } else {
        simple_middleware::Logger::Warn("Failed to load config, using defaults.");
    }

    Reset();
}

ControlComponent::~ControlComponent() {
    Stop();
}

void ControlComponent::Start() {
    if (running_) return;
    running_ = true;
    
    // 订阅控制指令
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    middleware.subscribe("visualizer/control", [this](const simple_middleware::Message& msg) {
        this->OnControlMessage(msg);
    });
    
    // 订阅模拟器真值 (作为反馈)
    middleware.subscribe("visualizer/data", [this](const simple_middleware::Message& msg) {
        this->OnSimulatorState(msg);
    });

    // 订阅规划轨迹
    middleware.subscribe("planning/trajectory", [this](const simple_middleware::Message& msg) {
        this->OnPlanningTrajectory(msg);
    });

    thread_ = std::thread(&ControlComponent::RunLoop, this);
    status_reporter_->Start();
    simple_middleware::Logger::Info("Started loop.");
}

void ControlComponent::Stop() {
    status_reporter_->Stop();
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void ControlComponent::Reset() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    // 只重置内部状态，不重置 frame_data_ (因为现在它是只读的反馈)
    target_point_.active = false;
    current_trajectory_.clear();
}

void ControlComponent::SetSpeed(double speed) {
    // 这里的 SetSpeed 不再直接改状态，而是应该触发控制指令发送
    // 但为了兼容现有逻辑，我们暂时只更新内部期望值，实际发送在 RunLoop
    // current_car_state_.set_speed(speed); // 不，不能直接改状态
}

void ControlComponent::SetSteering(double angle) {
    // 同上
}

void ControlComponent::SetTarget(double x, double y) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    target_point_.x = x;
    target_point_.y = y;
    target_point_.active = true;
    simple_middleware::Logger::Info("Set manual target point: (" + std::to_string(x) + ", " + std::to_string(y) + ")");
}

void ControlComponent::UpdateLookaheadPoint() {
    if (current_trajectory_.empty()) return;

    double cx = current_car_state_.position().x();
    double cy = current_car_state_.position().y();
    
    // ... (保持原有的寻点逻辑，只是把 car-> 改为 current_car_state_)
    
    double min_dist_sq = std::numeric_limits<double>::max();
    size_t closest_idx = 0;
    
    for (size_t i = 0; i < current_trajectory_.size(); ++i) {
        double dx = current_trajectory_[i].first - cx;
        double dy = current_trajectory_[i].second - cy;
        double d2 = dx*dx + dy*dy;
        if (d2 < min_dist_sq) {
            min_dist_sq = d2;
            closest_idx = i;
        }
    }
    
    // 寻找预瞄点
    for (size_t i = closest_idx; i < current_trajectory_.size(); ++i) {
        double dx = current_trajectory_[i].first - cx;
        double dy = current_trajectory_[i].second - cy;
        double dist = std::sqrt(dx*dx + dy*dy);
        
        if (dist >= lookahead_dist_) {
            target_point_.x = current_trajectory_[i].first;
            target_point_.y = current_trajectory_[i].second;
            target_point_.active = true;
            return;
        }
    }
    
    if (!current_trajectory_.empty()) {
        target_point_.x = current_trajectory_.back().first;
        target_point_.y = current_trajectory_.back().second;
        target_point_.active = true;
    }
}

void ControlComponent::ComputePurePursuitSteering(double dt) {
    if (!current_trajectory_.empty()) {
        UpdateLookaheadPoint();
    }

    if (!target_point_.active) return;

    // 使用当前反馈状态
    double cx = current_car_state_.position().x();
    double cy = current_car_state_.position().y();
    double heading = current_car_state_.heading();
    
    // ... (保持原有计算逻辑) ...
    double dx = target_point_.x - cx;
    double dy = target_point_.y - cy;
    double target_angle = std::atan2(dy, dx);
    double alpha = target_angle - heading;
    
    while (alpha > M_PI) alpha -= 2 * M_PI;
    while (alpha < -M_PI) alpha += 2 * M_PI;
    
    double dist = std::sqrt(dx*dx + dy*dy);
    
    if (dist < 1.0 && current_trajectory_.empty()) {
        // 到达目标，发送停车指令
        // 这里只是更新期望状态，实际发送在 RunLoop
        current_car_state_.set_speed(0.0); 
        target_point_.active = false;
        simple_middleware::Logger::Info("Target reached!");
        return;
    }
    
    double steer = std::atan2(2.0 * wheelbase_ * std::sin(alpha), dist);
    steer = std::max(-max_steer_, std::min(steer, max_steer_));
    
    // 这里我们把计算结果存入 current_car_state_ 作为"控制输出"暂存
    // 这是一个 hack，理想情况下应该有独立的 command 结构
    current_car_state_.set_steering_angle(steer);
}

void ControlComponent::RunLoop() {
    const double dt = 0.1; // 100ms
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();

    while (running_) {
        // 1. 计算控制量
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            ComputePurePursuitSteering(dt);
            
            // 2. 发送控制指令给 Simulator
            senseauto::demo::ControlCommand cmd;
            cmd.set_cmd("actuate");
            // 注意：这里我们用 value 存期望速度，target.x 存期望转角
            // 这是为了复用现有 Proto 结构的 hack
            cmd.set_value(current_car_state_.speed()); 
            cmd.mutable_target()->set_x(current_car_state_.steering_angle());
            
            std::string payload;
            cmd.SerializeToString(&payload);
            middleware.publish("control/command", payload);
        }

        // 3. Control 不再负责发布 visualizer/data
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void ControlComponent::OnSimulatorState(const simple_middleware::Message& msg) {
    senseauto::demo::FrameData frame;
    if (frame.ParseFromString(msg.data)) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (frame.has_car_state()) {
            // 更新本地反馈
            // 注意：不要覆盖 speed/steering，因为那是我们的控制目标
            // 我们只更新位置信息作为反馈
            auto* pos = current_car_state_.mutable_position();
            pos->set_x(frame.car_state().position().x());
            pos->set_y(frame.car_state().position().y());
            current_car_state_.set_heading(frame.car_state().heading());
            
            // 实际上这里的 speed 应该是测量速度，但我们的 PID 简单，先混用
        }
    }
}

void ControlComponent::OnControlMessage(const simple_middleware::Message& msg) {
    std::string err;
    Json json = Json::parse(msg.data, err);
    if (!err.empty()) return;
    
    std::string type = json["type"].string_value();
    
    if (type == "set_target") {
        double x = json["x"].number_value();
        double y = json["y"].number_value();
        SetTarget(x, y);
    } else if (type == "reset") {
        Reset();
    }
}


// 删除 GetSerializedData 和 OnPerceptionObstacles


void ControlComponent::OnPlanningTrajectory(const simple_middleware::Message& msg) {
    std::string err;
    Json json = Json::parse(msg.data, err);
    if (!err.empty()) return;
    
    // 解析 planning/trajectory 消息 (FrameData 结构)
    // 注意：planning 发送的也是 FrameData 结构的 JSON
    if (json["trajectory"].is_array()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_trajectory_.clear();
        double target_v = -1.0;

        for (const auto& pt : json["trajectory"].array_items()) {
            current_trajectory_.push_back({pt["x"].number_value(), pt["y"].number_value()});
            if (target_v < 0 && pt["speed"].is_number()) {
                target_v = pt["speed"].number_value();
            }
        }
        
#include <simple_middleware/logger.hpp> // Add logger include

// ...

        if (!current_trajectory_.empty()) {
            // 收到新轨迹，激活自动驾驶模式
            if (target_v >= 0) {
                // 如果轨迹中包含速度信息 (来自 Planning 的 ACC 逻辑)，直接应用
                current_car_state_.set_speed(target_v);
            } else {
                // 兼容旧逻辑：如果车没速度，给一个默认启动速度
                if (current_car_state_.speed() < 1.0) {
                     current_car_state_.set_speed(auto_engage_speed_);
                }
            }
            // 使用 Logger
            simple_middleware::Logger::Info("Received trajectory with " + std::to_string(current_trajectory_.size()) + " points. Target V: " + std::to_string(target_v));
        }
    }
}
