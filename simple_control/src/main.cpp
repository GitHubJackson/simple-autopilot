#include "control_component.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main(int argc, char* argv[]) {
    std::cout << "=== Simple Control Module ===" << std::endl;
    std::cout << "Connecting to middleware..." << std::endl;

    ControlComponent control;
    control.Start();
    
    std::cout << "Control module running..." << std::endl;
    std::cout << "Press Ctrl+C to exit." << std::endl;
    
    // 保持主线程运行
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}
