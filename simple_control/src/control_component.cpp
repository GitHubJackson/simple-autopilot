#include "control_component.hpp"
#include <google/protobuf/util/json_util.h>
#include <iostream>
#include <chrono>
#include <cmath>
#include <algorithm>

ControlComponent::ControlComponent() : running_(false) {
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

    thread_ = std::thread(&ControlComponent::RunLoop, this);
    std::cout << "[Control] Started data generation loop." << std::endl;
}

void ControlComponent::Stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void ControlComponent::Reset() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    frame_data_.Clear();
    
    // 初始化车辆
    auto* car = frame_data_.mutable_car_state();
    car->mutable_position()->set_x(0.0);
    car->mutable_position()->set_y(0.0);
    car->set_heading(0.0);
    car->set_speed(0.0);
    car->set_steering_angle(0.0);

    // 初始化障碍物
    auto* obs1 = frame_data_.add_obstacles();
    obs1->set_id(1);
    obs1->mutable_position()->set_x(20.0);
    obs1->mutable_position()->set_y(10.0);
    obs1->set_type("cone");

    auto* obs2 = frame_data_.add_obstacles();
    obs2->set_id(2);
    obs2->mutable_position()->set_x(30.0);
    obs2->mutable_position()->set_y(-5.0);
    obs2->set_type("pedestrian");
    
    auto* obs3 = frame_data_.add_obstacles();
    obs3->set_id(3);
    obs3->mutable_position()->set_x(50.0);
    obs3->mutable_position()->set_y(20.0);
    obs3->set_type("car");

    time_accumulator_ = 0.0;
}

void ControlComponent::SetSpeed(double speed) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto* car = frame_data_.mutable_car_state();
    car->set_speed(std::max(0.0, std::min(speed, 30.0)));
    std::cout << "[Control] Set speed: " << car->speed() << std::endl;
}

void ControlComponent::SetSteering(double angle) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto* car = frame_data_.mutable_car_state();
    car->set_steering_angle(std::max(-MAX_STEER, std::min(angle, MAX_STEER)));
    std::cout << "[Control] Set steering: " << car->steering_angle() << std::endl;
}

void ControlComponent::SetTarget(double x, double y) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    target_point_.x = x;
    target_point_.y = y;
    target_point_.active = true;
    
    // 如果设置了目标点，自动给一点速度，否则车不动没法追踪
    auto* car = frame_data_.mutable_car_state();
    if (car->speed() < 1.0) {
        car->set_speed(5.0); // 默认 5m/s
    }
    
    std::cout << "[Control] Set target point: (" << x << ", " << y << ")" << std::endl;
}

void ControlComponent::ComputePurePursuitSteering(double dt) {
    if (!target_point_.active) return;

    auto* car = frame_data_.mutable_car_state();
    double cx = car->position().x();
    double cy = car->position().y();
    double heading = car->heading();
    
    // 1. 转换目标点到车辆坐标系
    double dx = target_point_.x - cx;
    double dy = target_point_.y - cy;
    
    // 旋转变换 (世界 -> 车辆)
    // local_x = dx * cos(heading) + dy * sin(heading)
    // local_y = -dx * sin(heading) + dy * cos(heading) 
    // 注意：这里使用的是基于车辆前轴中心的简化模型
    // 为了简单，我们使用几何关系计算 lookahead angle
    
    // 2. 计算 Alpha (目标点与车头朝向的夹角)
    double target_angle = std::atan2(dy, dx);
    double alpha = target_angle - heading;
    
    // 归一化角度到 [-PI, PI]
    while (alpha > M_PI) alpha -= 2 * M_PI;
    while (alpha < -M_PI) alpha += 2 * M_PI;
    
    // 3. 计算距离 (Lookahead Distance)
    double dist = std::sqrt(dx*dx + dy*dy);
    
    // 到达判定
    if (dist < 2.0) {
        car->set_speed(0.0);
        target_point_.active = false;
        std::cout << "[Control] Target reached!" << std::endl;
        return;
    }
    
    // 4. Pure Pursuit 公式: delta = atan(2 * L * sin(alpha) / dist)
    // 这里的 KP 可以理解为对横向误差的增益调节
    double steer = std::atan2(2.0 * WHEELBASE * std::sin(alpha), dist);
    
    // 限制转角
    steer = std::max(-MAX_STEER, std::min(steer, MAX_STEER));
    car->set_steering_angle(steer);
}

void ControlComponent::RunLoop() {
    int frame_id = 0;
    const double dt = 0.1; // 100ms
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();

    while (running_) {
        // 1. 更新物理状态
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            
            // 如果有目标点，计算自动转向
            ComputePurePursuitSteering(dt);
            
            auto* car = frame_data_.mutable_car_state();
            double speed = car->speed();
            double heading = car->heading();
            double steering = car->steering_angle();

            // 更新位置 (简单的单车模型)
            double dx = speed * std::cos(heading) * dt;
            double dy = speed * std::sin(heading) * dt;
            double dheading = (speed / WHEELBASE) * std::tan(steering) * dt;

            car->mutable_position()->set_x(car->position().x() + dx);
            car->mutable_position()->set_y(car->position().y() + dy);
            car->set_heading(heading + dheading);

            // 更新动态障碍物 (id=2)
            // ... (保持原样)

            for (int i = 0; i < frame_data_.obstacles_size(); ++i) {
                auto* obs = frame_data_.mutable_obstacles(i);
                if (obs->id() == 2) {
                    obs->mutable_position()->set_y(-5.0 + 2.0 * std::sin(time_accumulator_));
                }
            }
            time_accumulator_ += dt;
        }

        // 2. 序列化数据
        std::string json_data = GetSerializedData(frame_id++);

        // 3. 发布数据到中间件
        middleware.publish("visualizer/data", json_data);

        // 4. 等待 100ms
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

std::string ControlComponent::GetSerializedData(int frame_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    frame_data_.set_frame_id(frame_id);
    frame_data_.set_timestamp(std::time(nullptr));
    
    std::string json_string;
    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = false;
    auto status = google::protobuf::util::MessageToJsonString(frame_data_, &json_string, options);
    
    if (status.ok()) {
        if (!json_string.empty() && json_string.back() == '}') {
             json_string.pop_back();
             json_string += ", \"type\": \"frame_data\"}";
        }
        return json_string;
    } else {
        return "{}";
    }
}

void ControlComponent::OnControlMessage(const simple_middleware::Message& msg) {
    std::string cmd_json = msg.data;
    std::cout << "[Control] Received command: " << cmd_json << std::endl;
    
    std::string cmd = parseJsonString(cmd_json, "cmd");
    
    if (cmd == "reset") {
        Reset();
    } 
    else if (cmd == "set_speed") {
        double val = parseJsonDouble(cmd_json, "value");
        SetSpeed(val);
    }
    else if (cmd == "set_steer") {
        double val = parseJsonDouble(cmd_json, "value");
        SetSteering(val);
    }
    else if (cmd == "set_target") {
        // 解析 x, y (这里假设 json 格式: {cmd:set_target, x:10, y:20})
        // 注意：parseJsonDouble 是个很粗糙的实现，如果 json 里有嵌套或格式复杂可能会解析失败
        double x = parseJsonDouble(cmd_json, "x");
        double y = parseJsonDouble(cmd_json, "y");
        SetTarget(x, y);
    }
}

std::string ControlComponent::parseJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t start = json.find(search);
    if (start == std::string::npos) return "";
    start += search.length();
    while (start < json.length() && (json[start] == ' ' || json[start] == '\"')) start++;
    size_t end = start;
    while (end < json.length() && json[end] != '\"' && json[end] != ',' && json[end] != '}') end++;
    return json.substr(start, end - start);
}

double ControlComponent::parseJsonDouble(const std::string& json, const std::string& key) {
    std::string val = parseJsonString(json, key);
    if (val.empty()) return 0.0;
    try {
        return std::stod(val);
    } catch (...) {
        return 0.0;
    }
}
