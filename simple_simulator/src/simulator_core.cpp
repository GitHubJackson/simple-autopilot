#include "simulator_core.hpp"
#include <iostream>
#include <cmath>
#include <chrono>
#include <json11.hpp>
#include <google/protobuf/util/json_util.h>
#include <simple_middleware/logger.hpp> // Add logger include

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
    
    // 订阅控制命令（包括 reset）
    middleware.subscribe("visualizer/control", [this](const simple_middleware::Message& msg) {
        this->OnControlMessage(msg);
    });

    thread_ = std::thread(&SimulatorCore::RunLoop, this);
    status_reporter_->Start();
    simple_middleware::Logger::Info("Engine started. Physics running at 100Hz, publishing at 20Hz.");
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
    
    // 初始化障碍物（包含静态和动态障碍物）
    // 1. 静态挡路车 (Blocking Car) - 放在正前方 30m（静止）
    auto* obs1 = world_state_.add_obstacles();
    obs1->set_id(1);
    obs1->mutable_position()->set_x(30.0);
    obs1->mutable_position()->set_y(0.0);
    obs1->set_type("car");
    obs1->set_length(4.5);
    obs1->set_width(1.8);
    obs1->set_heading(0.0);  // 朝向X轴正方向

    // 2. 动态障碍物 - 同向慢车（在左侧车道，向前移动）
    auto* obs2 = world_state_.add_obstacles();
    obs2->set_id(2);
    obs2->mutable_position()->set_x(50.0);
    obs2->mutable_position()->set_y(3.5);
    obs2->set_type("car");
    obs2->set_length(4.5);
    obs2->set_width(1.8);
    obs2->set_heading(0.0);
    
    // 设置动态障碍物的速度（3 m/s，约10.8 km/h，模拟慢车）
    DynamicObstacle dyn_obs2;
    dyn_obs2.speed = 3.0;  // 3 m/s
    dyn_obs2.heading = 0.0;  // 沿X轴正方向
    dyn_obs2.vx = dyn_obs2.speed * std::cos(dyn_obs2.heading);
    dyn_obs2.vy = dyn_obs2.speed * std::sin(dyn_obs2.heading);
    dynamic_obstacles_[2] = dyn_obs2;
    
    // 3. 动态障碍物 - 对向车辆（在右侧车道，向后移动）
    auto* obs3 = world_state_.add_obstacles();
    obs3->set_id(3);
    obs3->mutable_position()->set_x(80.0);
    obs3->mutable_position()->set_y(-3.5);
    obs3->set_type("car");
    obs3->set_length(4.5);
    obs3->set_width(1.8);
    obs3->set_heading(M_PI);  // 朝向X轴负方向（对向）
    
    // 设置动态障碍物的速度（5 m/s，约18 km/h，模拟对向车）
    DynamicObstacle dyn_obs3;
    dyn_obs3.speed = 5.0;  // 5 m/s
    dyn_obs3.heading = M_PI;  // 沿X轴负方向
    dyn_obs3.vx = dyn_obs3.speed * std::cos(dyn_obs3.heading);
    dyn_obs3.vy = dyn_obs3.speed * std::sin(dyn_obs3.heading);
    dynamic_obstacles_[3] = dyn_obs3;
    
    simple_middleware::Logger::Info("Simulator: Initialized scenario with " 
        + std::to_string(world_state_.obstacles_size()) + " obstacles ("
        + std::to_string(dynamic_obstacles_.size()) + " dynamic)");
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
            
            static int log_counter = 0;
            if (log_counter++ % 100 == 0) { // 每 100 次（1秒）输出一次
                simple_middleware::Logger::Debug(
                    "Simulator: Received control command - speed=" + std::to_string(target_speed_) +
                    ", steering=" + std::to_string(target_steering_));
            }
        }
    } else {
        simple_middleware::Logger::Warn("Simulator: Failed to parse control command");
    }
}

void SimulatorCore::OnControlMessage(const simple_middleware::Message& msg) {
    // 解析 JSON 控制消息（来自前端）
    std::string err;
    Json json = Json::parse(msg.data, err);
    if (!err.empty()) {
        return; // 不是 JSON 格式，忽略
    }
    
    // 支持两种格式：前端可能发送 "cmd" 或 "type"
    std::string cmd = json["cmd"].string_value();
    if (cmd.empty()) {
        cmd = json["type"].string_value();
    }
    
    if (cmd == "reset") {
        std::lock_guard<std::mutex> lock(state_mutex_);
        // 重置车辆状态到初始位置
        auto* car = world_state_.mutable_car_state();
        car->mutable_position()->set_x(0.0);
        car->mutable_position()->set_y(0.0);
        car->set_heading(0.0);
        car->set_speed(0.0);
        car->set_steering_angle(0.0);
        
        // 重置控制目标
        target_speed_ = 0.0;
        target_steering_ = 0.0;
        
        simple_middleware::Logger::Info("Simulator: Reset vehicle to origin");
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
    
    // 更新动态障碍物位置
    UpdateDynamicObstacles(dt);
}

void SimulatorCore::UpdateDynamicObstacles(double dt) {
    // 遍历所有障碍物，更新动态障碍物的位置
    for (int i = 0; i < world_state_.obstacles_size(); ++i) {
        auto* obs = world_state_.mutable_obstacles(i);
        int32_t obs_id = obs->id();
        
        // 检查是否是动态障碍物
        auto it = dynamic_obstacles_.find(obs_id);
        if (it != dynamic_obstacles_.end()) {
            const DynamicObstacle& dyn_obs = it->second;
            
            // 更新位置（匀速直线运动）
            double new_x = obs->position().x() + dyn_obs.vx * dt;
            double new_y = obs->position().y() + dyn_obs.vy * dt;
            
            obs->mutable_position()->set_x(new_x);
            obs->mutable_position()->set_y(new_y);
            obs->set_heading(dyn_obs.heading);
            
            // 如果障碍物移动太远，重置到初始位置（循环场景）
            if (new_x > 150.0 || new_x < -50.0) {
                // 重置到初始位置（根据障碍物ID）
                if (obs_id == 2) {
                    obs->mutable_position()->set_x(50.0);
                    obs->mutable_position()->set_y(3.5);
                } else if (obs_id == 3) {
                    obs->mutable_position()->set_x(80.0);
                    obs->mutable_position()->set_y(-3.5);
                }
            }
        }
    }
}

void SimulatorCore::RunLoop() {
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    const double dt = 0.01; // 10ms (100Hz)
    int frame_id = 0;
    static int no_publish_count = 0;

    while (running_) {
        try {
            auto start = std::chrono::steady_clock::now();

            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                StepPhysics(dt);
                
                // 更新时间戳
                world_state_.set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
                world_state_.set_frame_id(frame_id++);
                
                // 发布节流：物理仿真保持 100Hz，但只每 PUBLISH_INTERVAL 帧发布一次给前端
                // 这样前端只需要处理 20Hz 的数据，避免过载
                publish_counter_++;
                if (publish_counter_ >= PUBLISH_INTERVAL) {
                    publish_counter_ = 0;
                    
                    // 序列化并广播真值
                    std::string serialized;
                    // Hack: 使用 "visualizer/data" 作为 topic 以兼容现有的 Sensor/Visualizer
                    // 它们之前是订阅 Control 发出的这个 topic
                    if (world_state_.SerializeToString(&serialized)) {
                        bool published = middleware.publish("visualizer/data", serialized);
                        if (published) {
                            no_publish_count = 0;
                            static int pub_count = 0;
                            if (pub_count++ % 30 == 0 || pub_count == 1) { // 每 30 次或第一次打印
                                simple_middleware::Logger::Debug("Simulator: Published visualizer/data, frame_id=" 
                                    + std::to_string(world_state_.frame_id())
                                    + ", car_state: x=" + std::to_string(world_state_.car_state().position().x())
                                    + ", y=" + std::to_string(world_state_.car_state().position().y())
                                    + ", speed=" + std::to_string(world_state_.car_state().speed())
                                    + ", size=" + std::to_string(serialized.size()) + " bytes");
                            }
                        } else {
                            no_publish_count++;
                            if (no_publish_count % 10 == 0) {
                                simple_middleware::Logger::Warn("Simulator: Failed to publish visualizer/data (count=" 
                                    + std::to_string(no_publish_count) + ")");
                            }
                        }
                    } else {
                        static int serialize_fail_count = 0;
                        if (serialize_fail_count++ % 10 == 0) {
                            simple_middleware::Logger::Warn("Simulator: Failed to serialize world_state");
                        }
                    }
                }
            }

            auto end = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            if (elapsed.count() < 10) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10) - elapsed);
            }
        } catch (const std::exception& e) {
            static int error_count = 0;
            if (error_count++ % 10 == 0) {
                simple_middleware::Logger::Error("Simulator: Exception in RunLoop: " + std::string(e.what()));
            }
        } catch (...) {
            static int error_count = 0;
            if (error_count++ % 10 == 0) {
                simple_middleware::Logger::Error("Simulator: Unknown exception in RunLoop");
            }
        }
    }
}

