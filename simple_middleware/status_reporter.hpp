/*
 * @Desc: Node Status Reporter Helper
 * @Author: Assistant
 * @Date: 2025/12/20
 */

#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include "pub_sub_middleware.hpp"
#include "common_msgs/system_status.pb.h"

namespace simple_middleware {

class StatusReporter {
public:
    explicit StatusReporter(const std::string& node_name);
    ~StatusReporter();

    void Start();
    void Stop();
    
    void SetStatus(senseauto::demo::NodeStatus::State state, const std::string& msg);

private:
    void ReportLoop();

    std::string node_name_;
    senseauto::demo::NodeStatus current_status_;
    mutable std::mutex status_mutex_;
    
    std::thread report_thread_;
    std::atomic<bool> running_{false};
};

} // namespace simple_middleware

