#pragma once

#include <common_msgs/map_data.pb.h>
#include <common_msgs/visualizer_data.pb.h>
#include <simple_middleware/pub_sub_middleware.hpp>
#include <simple_middleware/status_reporter.hpp>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <memory>
#include <vector>

class MapComponent {
public:
    MapComponent();
    ~MapComponent();

    void Start();
    void Stop();

private:
    void RunLoop();
    void GenerateLaneData();

private:
    std::atomic<bool> running_;
    std::thread thread_;
    std::mutex state_mutex_;

    senseauto::demo::MapData map_data_;

    std::unique_ptr<simple_middleware::StatusReporter> status_reporter_;
};
