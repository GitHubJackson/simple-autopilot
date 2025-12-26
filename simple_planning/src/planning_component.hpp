#pragma once

#include <common_msgs/visualizer_data.pb.h>
#include <simple_middleware/pub_sub_middleware.hpp>
#include <simple_middleware/status_reporter.hpp>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <memory>
#include <vector>
#include "json11.hpp" // 引入 json11
#include <simple_middleware/config_manager.hpp>

enum class PlanningState {
    CRUISE,
    FOLLOW,
    STOP
};

class PlanningComponent {
public:
    PlanningComponent();
    ~PlanningComponent();

    void Start();
    void Stop();

private:
    void RunLoop();
    void OnControlMessage(const simple_middleware::Message& msg);
    void OnCarStatus(const simple_middleware::Message& msg);
    void OnPerceptionObstacles(const simple_middleware::Message& msg);

    void GenerateTrajectory();
    
private:
    std::atomic<bool> running_;
    std::thread thread_;
    std::mutex state_mutex_;

    struct {
        double x = 0.0;
        double y = 0.0;
        double heading = 0.0;
    } current_pose_;

    struct {
        double x = 0.0;
        double y = 0.0;
        bool active = false;
    } target_point_;

    struct TrajectoryPoint {
        double x;
        double y;
        double v;
    };
    std::vector<TrajectoryPoint> current_trajectory_;

    std::unique_ptr<simple_middleware::StatusReporter> status_reporter_;
    
    // Config parameters
    int loop_rate_ms_ = 100;
    double target_reach_threshold_ = 1.0;
    double default_cruise_speed_ = 5.0;
    double follow_distance_ = 15.0;
    double acc_kp_ = 0.5;

    // State
    PlanningState state_ = PlanningState::CRUISE;
    senseauto::demo::Obstacle closest_obstacle_;
    bool has_obstacle_ = false;
};
