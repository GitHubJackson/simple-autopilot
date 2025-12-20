/*
 * @Desc: Node Status Reporter Implementation
 * @Author: Assistant
 * @Date: 2025/12/20
 */

#include "status_reporter.hpp"
#include <chrono>
#include <iostream>

namespace simple_middleware {

StatusReporter::StatusReporter(const std::string& node_name) 
    : node_name_(node_name) {
    current_status_.set_node_name(node_name);
    current_status_.set_state(senseauto::demo::NodeStatus::OK);
    current_status_.set_message("Node initialized");
}

StatusReporter::~StatusReporter() {
    Stop();
}

void StatusReporter::Start() {
    if (running_) return;
    running_ = true;
    report_thread_ = std::thread(&StatusReporter::ReportLoop, this);
}

void StatusReporter::Stop() {
    if (!running_) return;
    running_ = false;
    if (report_thread_.joinable()) {
        report_thread_.join();
    }
}

void StatusReporter::SetStatus(senseauto::demo::NodeStatus::State state, const std::string& msg) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    current_status_.set_state(state);
    current_status_.set_message(msg);
}

void StatusReporter::ReportLoop() {
    auto& middleware = PubSubMiddleware::getInstance();
    
    while (running_) {
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            // Update timestamp
            auto now = std::chrono::system_clock::now().time_since_epoch();
            auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            current_status_.set_timestamp(millis);
            
            std::string data;
            if (current_status_.SerializeToString(&data)) {
                middleware.publish("system/node_status", data);
            }
        }
        // Heartbeat every 1 second
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

} // namespace simple_middleware

