/*
 * @Desc: SenseAuto Visualizer Demo - Refactored (Stage 4)
 * @Author: Lucas & AI Assistant
 * @Date: 2025/12/17
 */

#include "server/visualizer_server.hpp"

#define PORT "8082"

int main(int argc, char *argv[]) {
    VisualizerServer viz_server;
    
    if (viz_server.Init(PORT)) {
        viz_server.WaitForExit();
    }

    return 0;
}
