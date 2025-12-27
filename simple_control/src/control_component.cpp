#include "control_component.hpp"
#include <simple_middleware/logger.hpp> // Add logger include
#include <google/protobuf/util/json_util.h>
#include <json11.hpp>
#include <simple_middleware/config_manager.hpp>
#include <iostream>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <arpa/inet.h> // for ntohl

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

    // 订阅规划轨迹（完整消息）
    int64_t traj_sub_id = middleware.subscribe("planning/trajectory", [this](const simple_middleware::Message& msg) {
        simple_middleware::Logger::Info("Control: Received planning/trajectory message, size=" + std::to_string(msg.data.size()));
        this->OnPlanningTrajectory(msg);
    });
    if (traj_sub_id >= 0) {
        simple_middleware::Logger::Info("Control: Subscribed to planning/trajectory (ID: " + std::to_string(traj_sub_id) + ")");
    } else {
        simple_middleware::Logger::Error("Control: Failed to subscribe to planning/trajectory");
    }
    
    // 订阅规划轨迹分片
    int64_t chunk_sub_id = middleware.subscribe("planning/trajectory/chunk", [this](const simple_middleware::Message& msg) {
        this->OnPlanningTrajectoryChunk(msg);
    });
    if (chunk_sub_id >= 0) {
        simple_middleware::Logger::Info("Control: Subscribed to planning/trajectory/chunk (ID: " + std::to_string(chunk_sub_id) + ")");
    } else {
        simple_middleware::Logger::Error("Control: Failed to subscribe to planning/trajectory/chunk");
    }

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
    // 重置内部状态
    target_point_.active = false;
    current_trajectory_.clear();
    manual_control_mode_ = false;
    waiting_for_trajectory_ = false; // 重置等待标志
    // 重置控制输出，让车辆立即停止
    current_car_state_.set_speed(0.0);
    current_car_state_.set_steering_angle(0.0);
}

void ControlComponent::SetSpeed(double speed) {
    // 手动设置目标速度（覆盖自动控制）
    std::lock_guard<std::mutex> lock(state_mutex_);
    // 限制速度范围
    speed = std::max(0.0, std::min(speed, max_speed_));
    current_car_state_.set_speed(speed);
    manual_control_mode_ = true; // 设置为手动控制模式
    simple_middleware::Logger::Info("Manual speed set to: " + std::to_string(speed) + " m/s");
}

void ControlComponent::SetSteering(double angle) {
    // 手动设置目标转向角（覆盖自动控制）
    std::lock_guard<std::mutex> lock(state_mutex_);
    // 限制转向角范围
    angle = std::max(-max_steer_, std::min(angle, max_steer_));
    current_car_state_.set_steering_angle(angle);
    manual_control_mode_ = true; // 设置为手动控制模式
    simple_middleware::Logger::Info("Manual steering set to: " + std::to_string(angle) + " rad");
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
    // 如果是手动控制模式，不执行自动控制算法
    if (manual_control_mode_) {
        return;
    }
    
    // 【修复1】如果正在等待规划轨迹（收到 set_target 后），且没有轨迹，则等待
    if (waiting_for_trajectory_ && current_trajectory_.empty()) {
        // 等待 Planning 生成轨迹
        current_car_state_.set_speed(0.0);
        current_car_state_.set_steering_angle(0.0);
        return;
    }
    
    // 如果有轨迹，使用轨迹跟踪
    if (!current_trajectory_.empty()) {
        UpdateLookaheadPoint();
    }

    if (!target_point_.active) {
        // 没有目标点，设置速度为0
        current_car_state_.set_speed(0.0);
        current_car_state_.set_steering_angle(0.0);
        return;
    }

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
        current_car_state_.set_speed(0.0); 
        current_car_state_.set_steering_angle(0.0);
        target_point_.active = false;
        simple_middleware::Logger::Info("Target reached!");
        return;
    }
    
    // 计算转向角
    double steer = std::atan2(2.0 * wheelbase_ * std::sin(alpha), dist);
    steer = std::max(-max_steer_, std::min(steer, max_steer_));
    
    // 设置目标速度：如果有目标点，使用自动速度
    double target_speed = auto_engage_speed_;
    if (dist < 5.0) {
        // 接近目标时减速
        target_speed = std::min(auto_engage_speed_, dist * 0.5);
    }
    
    // 这里我们把计算结果存入 current_car_state_ 作为"控制输出"暂存
    // 这是一个 hack，理想情况下应该有独立的 command 结构
    current_car_state_.set_speed(target_speed);
    current_car_state_.set_steering_angle(steer);
}

void ControlComponent::RunLoop() {
    const double dt = 0.1; // 100ms
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    static int log_counter = 0;

    while (running_) {
        // 1. 计算控制量
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            ComputePurePursuitSteering(dt);
            
            // 调试日志：每 50 次循环（5秒）输出一次状态
            if (log_counter++ % 50 == 0) {
                simple_middleware::Logger::Debug(
                    "Control Loop: speed=" + std::to_string(current_car_state_.speed()) +
                    ", steering=" + std::to_string(current_car_state_.steering_angle()) +
                    ", manual_mode=" + std::string(manual_control_mode_ ? "true" : "false") +
                    ", target_active=" + std::string(target_point_.active ? "true" : "false"));
            }
            
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
    if (!err.empty()) {
        simple_middleware::Logger::Warn("Failed to parse control message: " + err);
        return;
    }
    
    // 支持两种格式：前端可能发送 "cmd" 或 "type"
    std::string cmd = json["cmd"].string_value();
    if (cmd.empty()) {
        cmd = json["type"].string_value();
    }
    
    if (cmd == "set_target") {
        double x = json["x"].number_value();
        double y = json["y"].number_value();
        SetTarget(x, y);
        manual_control_mode_ = false; // 设置目标点后切换到自动模式
        // 【修复1】清空当前轨迹，设置等待标志，等待 Planning 生成新轨迹
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            current_trajectory_.clear();
            waiting_for_trajectory_ = true; // 设置等待标志
        }
        simple_middleware::Logger::Info("Received set_target: (" + std::to_string(x) + ", " + std::to_string(y) + "), waiting for planning trajectory...");
    } else if (cmd == "set_speed") {
        double speed = json["value"].number_value();
        SetSpeed(speed);
        manual_control_mode_ = true; // 手动设置速度后切换到手动模式
    } else if (cmd == "set_steer") {
        double angle = json["value"].number_value();
        SetSteering(angle);
        manual_control_mode_ = true; // 手动设置转向后切换到手动模式
    } else if (cmd == "reset") {
        Reset();
        manual_control_mode_ = false;
        simple_middleware::Logger::Info("Received reset command");
    } else if (cmd == "stop") {
        // 紧急停车
        std::lock_guard<std::mutex> lock(state_mutex_);
        target_point_.active = false;
        current_car_state_.set_speed(0.0);
        current_car_state_.set_steering_angle(0.0);
        manual_control_mode_ = false;
        simple_middleware::Logger::Info("Received stop command");
    } else if (!cmd.empty()) {
        simple_middleware::Logger::Debug("Unknown control command: " + cmd);
    }
}


// 删除 GetSerializedData 和 OnPerceptionObstacles


void ControlComponent::OnPlanningTrajectory(const simple_middleware::Message& msg) {
    simple_middleware::Logger::Info("Control: OnPlanningTrajectory called, data_size=" + std::to_string(msg.data.size()));
    
    std::string err;
    Json json = Json::parse(msg.data, err);
    if (!err.empty()) {
        simple_middleware::Logger::Error("Control: Failed to parse planning/trajectory JSON: " + err);
        return;
    }
    
    simple_middleware::Logger::Info("Control: Parsed JSON successfully, checking trajectory array...");
    
    // 解析 planning/trajectory 消息 (FrameData 结构)
    // 注意：planning 发送的也是 FrameData 结构的 JSON
    if (json["trajectory"].is_array()) {
        simple_middleware::Logger::Info("Control: Found trajectory array with " + std::to_string(json["trajectory"].array_items().size()) + " points");
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
            // 收到新轨迹，清除等待标志，激活自动驾驶模式
            waiting_for_trajectory_ = false; // 收到轨迹后，清除等待标志
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

void ControlComponent::OnPlanningTrajectoryChunk(const simple_middleware::Message& msg) {
    if (msg.data.size() < 16) {
        static int error_count = 0;
        if (error_count++ % 100 == 0) {
            simple_middleware::Logger::Warn("Control: Trajectory chunk too small: " + std::to_string(msg.data.size()) + " bytes");
        }
        return;
    }
    
    const uint32_t* header = reinterpret_cast<const uint32_t*>(msg.data.data());
    uint32_t frame_id = ntohl(header[0]);
    uint32_t chunk_id = ntohl(header[1]);
    uint32_t total_chunks = ntohl(header[2]);
    uint32_t chunk_size = ntohl(header[3]);
    
    if (msg.data.size() != 16 + chunk_size) {
        static int error_count = 0;
        if (error_count++ % 100 == 0) {
            simple_middleware::Logger::Warn("Control: Trajectory chunk size mismatch: expected " + std::to_string(16 + chunk_size) 
                + ", got " + std::to_string(msg.data.size()));
        }
        return;
    }
    
    // 提取分片数据
    std::string chunk_data = msg.data.substr(16, chunk_size);
    
    bool should_process = false;
    std::string full_data;
    
    {
        std::lock_guard<std::mutex> lock(trajectory_chunk_mutex_);
        
        // 获取或创建分片缓冲区
        auto& buffer = trajectory_chunk_buffers_[frame_id];
        buffer.frame_id = frame_id;
        buffer.total_chunks = total_chunks;
        buffer.last_update = std::chrono::steady_clock::now();
        
        // 确保 chunks 数组大小足够
        if (buffer.chunks.size() < total_chunks) {
            buffer.chunks.resize(total_chunks);
        }
        
        // 存储分片数据
        if (chunk_id < total_chunks) {
            buffer.chunks[chunk_id] = chunk_data;
        }
        
        // 检查是否所有分片都已收到
        bool all_received = true;
        for (size_t i = 0; i < total_chunks; ++i) {
            if (buffer.chunks[i].empty()) {
                all_received = false;
                break;
            }
        }
        
        if (all_received) {
            // 重组完整数据
            full_data.reserve(total_chunks * chunk_data.size());
            for (const auto& chunk : buffer.chunks) {
                full_data += chunk;
            }
            
            should_process = true;
            trajectory_chunk_buffers_.erase(frame_id);
        }
        
        // 清理超时的分片缓冲区（1秒超时）
        auto now = std::chrono::steady_clock::now();
        for (auto it = trajectory_chunk_buffers_.begin(); it != trajectory_chunk_buffers_.end();) {
            if (now - it->second.last_update > std::chrono::seconds(1)) {
                simple_middleware::Logger::Warn("Control: Trajectory chunk timeout for frame " + std::to_string(it->first));
                it = trajectory_chunk_buffers_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // 在锁外处理完整数据
    if (should_process) {
        simple_middleware::Logger::Info("Control: Reassembled trajectory from " + std::to_string(total_chunks) + " chunks");
        // 构造完整的 Message 并调用 OnPlanningTrajectory
        simple_middleware::Message full_msg("planning/trajectory", full_data);
        full_msg.timestamp = msg.timestamp;
        OnPlanningTrajectory(full_msg);
    }
}
