# SenseAuto Node Visualizer Demos

这是一个基于 C++ 的简易自动驾驶软件栈演示项目，展示了现代自动驾驶系统的核心架构设计，包括模块化、去中心化通信、公共消息定义以及 Web 可视化。

## 🏗️ 架构概览

本项目采用典型的分层架构，各模块功能如下：

```
demos/
├── 3rdparty/          # [基础设施层] 统一管理外部依赖 (Protobuf, CivetWeb等)
├── common_msgs/       # [公共协议层] 定义全车通用的 Protobuf 消息格式
├── simple_middleware/ # [通信层] 基于 UDP 广播的发布/订阅中间件 (类似 ROS/CyberRT)
├── simple_control/    # [算法层] 车辆控制模块，包含纯追踪算法 (Pure Pursuit)
└── simple_visualizer/ # [交互层] 基于 WebSocket 和 Canvas 的 Web 可视化终端
```

## 🚀 快速开始

### 1. 编译

项目提供了一键编译脚本，会自动处理所有模块的依赖关系：

```bash
cd demos
./build_all.sh
```

### 2. 运行

为了完整体验系统，你需要开启三个终端窗口，分别运行以下组件：

**终端 1: 中间件监视器 (Monitor)**
用于实时监控网络中的数据流，类似 `rostopic list/echo`。

```bash
./demos/install/bin/middleware_monitor
```

**终端 2: 控制模块 (Control)**
核心业务逻辑，负责车辆运动学模拟和路径追踪控制。

```bash
./demos/simple_control/build/control_server
```

**终端 3: 可视化模块 (Visualizer)**
Web 服务器，负责将数据推送到浏览器。

```bash
./demos/simple_visualizer/build/server
```

### 3. 体验

1.  打开浏览器访问 `http://localhost:8082`
2.  你将看到一个包含车辆、障碍物和网格的实时视图。
3.  **手动控制**：拖动左侧的 "Target Speed" 和 "Steering" 滑块来控制车辆。
4.  **自动驾驶**：点击 "Go to Target (30,10)"，车辆将使用纯追踪算法自动规划并行驶到目标点。

## 🧩 模块详解

### Common Msgs

- 存放 `.proto` 文件，定义了 `CarState` (车辆状态) 和 `Obstacle` (障碍物) 等核心数据结构。
- 编译生成独立的动态库，供 Control 和 Visualizer 链接使用，避免循环依赖。

### Simple Middleware

- 一个轻量级的通信库，实现了 `publish/subscribe` 模式。
- 底层使用 UDP 广播，支持跨进程通信。
- 提供了 `middleware_monitor` 工具用于调试。

### Simple Control

- **功能**：模拟车辆物理运动（单车模型），生成模拟数据。
- **算法**：内置 Pure Pursuit (纯追踪) 算法，支持路径点跟踪。
- **通信**：
  - 发布：`visualizer/data` (车辆状态)
  - 订阅：`visualizer/control` (来自前端的控制指令)

### Simple Visualizer

- **后端**：使用 CivetWeb 搭建 HTTP/WebSocket 服务器。
- **前端**：HTML5 Canvas 绘制，支持 60FPS 流畅渲染。
- **功能**：实时显示车辆轨迹、障碍物，并提供交互控制面板。

## 🛠️ 依赖项

- CMake (>= 3.10)
- C++17 编译器
- Protobuf (Google Protocol Buffers)
- Pthread

## 📝 学习要点

通过这个项目，你可以学习到：

1.  **大型 C++ 项目构建**：如何使用 CMake 管理多模块依赖和第三方库。
2.  **解耦架构**：如何通过中间件和公共协议库将业务逻辑与可视化分离。
3.  **自动驾驶算法**：基础的车辆运动学模型和纯追踪控制算法实现。
4.  **全栈开发**：从底层 C++ 网络编程到前端 JavaScript 可视化的完整链路。
