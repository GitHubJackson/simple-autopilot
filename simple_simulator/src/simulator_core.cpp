#include "simulator_core.hpp"
#include <iostream>
#include <cmath>
#include <chrono>
#include <json11.hpp>
#include <google/protobuf/util/json_util.h>
#include <simple_middleware/logger.hpp> // Add logger include

using namespace json11;

SimulatorCore::SimulatorCore() : running_(false) {
    status_reporter_ = std::make_unique<simple_middleware::StatusReporter>("SimulatorNode");
    InitScenario();
}

SimulatorCore::~SimulatorCore() {
    Stop();
}

void SimulatorCore::Start() {
    if (running_) return;
    running_ = true;

    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    
    // 订阅来自 Control 模块的物理控制指令
    middleware.subscribe("control/command", [this](const simple_middleware::Message& msg) {
        this->OnControlCommand(msg);
    });

    thread_ = std::thread(&SimulatorCore::RunLoop, this);
    status_reporter_->Start();
    simple_middleware::Logger::Info("Engine started. Physics running at 100Hz.");
}

void SimulatorCore::Stop() {
    status_reporter_->Stop();
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void SimulatorCore::InitScenario() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    world_state_.Clear();
    
    // 初始化车辆
    auto* car = world_state_.mutable_car_state();
    car->mutable_position()->set_x(0.0);
    car->mutable_position()->set_y(0.0);
    car->set_heading(0.0);
    car->set_speed(0.0);
    
    // 初始化一些静态障碍物 (这就是所谓的“真值”)
    // 1. 挡路车 (Blocking Car) - 放在正前方 30m
    auto* obs1 = world_state_.add_obstacles();
    obs1->set_id(1);
    obs1->mutable_position()->set_x(30.0);
    obs1->mutable_position()->set_y(0.0);
    obs1->set_type("car");
    obs1->set_length(4.5);
    obs1->set_width(1.8);

    // 2. 远处的车 (Left Lane)
    auto* obs2 = world_state_.add_obstacles();
    obs2->set_id(2);
    obs2->mutable_position()->set_x(60.0);
    obs2->mutable_position()->set_y(3.5);
    obs2->set_type("car");
    obs2->set_length(4.5);
    obs2->set_width(1.8);
}

void SimulatorCore::OnControlCommand(const simple_middleware::Message& msg) {
    // 解析 ControlCommand
    senseauto::demo::ControlCommand cmd;
    if (cmd.ParseFromString(msg.data)) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        
        // 简单模拟：假设 Control 发来的是目标速度和转角
        // 实际上应该是油门/刹车踏板开度，这里简化处理
        if (cmd.cmd() == "actuate") {
            target_speed_ = cmd.value(); // 复用 value 存速度
            target_steering_ = cmd.target().x(); // hack: 复用 x 存转角
        }
    }
}

void SimulatorCore::StepPhysics(double dt) {
    auto* car = world_state_.mutable_car_state();
    
    // 简单的惯性模拟 (一阶滞后)
    double current_speed = car->speed();
    double speed_diff = target_speed_ - current_speed;
    car->set_speed(current_speed + speed_diff * dt * 2.0); // 假设加速能力
    
    car->set_steering_angle(target_steering_); // 假设转向瞬间响应

    // 运动学积分 (Bicycle Model)
    double speed = car->speed();
    double heading = car->heading();
    double steering = car->steering_angle();

    double dx = speed * std::cos(heading) * dt;
    double dy = speed * std::sin(heading) * dt;
    double dheading = (speed / WHEELBASE) * std::tan(steering) * dt;

    car->mutable_position()->set_x(car->position().x() + dx);
    car->mutable_position()->set_y(car->position().y() + dy);
    car->set_heading(heading + dheading);
}

void SimulatorCore::RunLoop() {
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    const double dt = 0.01; // 10ms (100Hz)
    int frame_id = 0;

    while (running_) {
        auto start = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            StepPhysics(dt);
            
            // 更新时间戳
            world_state_.set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            world_state_.set_frame_id(frame_id++);
            
            // 序列化并广播真值
            std::string serialized;
            // Hack: 使用 "visualizer/data" 作为 topic 以兼容现有的 Sensor/Visualizer
            // 它们之前是订阅 Control 发出的这个 topic
            if (world_state_.SerializeToString(&serialized)) {
                middleware.publish("visualizer/data", serialized);
            }
        }

        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        if (elapsed.count() < 10) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10) - elapsed);
        }
    }
}

