# AutoPilot System Demos

这是一个基于 C++ 的简易自动驾驶软件栈演示项目，展示了现代自动驾驶系统的核心架构设计，包括模块化、去中心化通信、公共消息定义以及 Web 可视化。

## 🏗️ 架构概览

本项目采用典型的分层架构，各模块功能如下：

```
demos/
├── 3rdparty/          # [基础设施层] 统一管理外部依赖 (Protobuf, CivetWeb, json11等)
├── common_msgs/       # [公共协议层] 定义全车通用的 Protobuf 消息格式 (FrameData, ControlCommand等)
├── simple_middleware/ # [通信层] 基于 UDP 广播的发布/订阅中间件 (类似 ROS/CyberRT)
├── simple_daemon/     # [管理层] 负责各节点进程的生命周期管理与健康监控
├── system_monitor/    # [监控层] 集成了节点状态与网络流量的实时监视器
├── simple_map/        # [地图层] 提供地图静态车道线等数据
├── simple_simulator/  # [仿真层] 物理仿真引擎，维护世界真值并进行运动学积分
├── simple_sensor/     # [传感器层] 基于仿真真值生成传感器观测数据
├── simple_perception/ # [感知层] 负责障碍物检测与跟踪
├── simple_prediction/ # [预测层] 预测障碍物未来轨迹
├── simple_planning/   # [规划决策层] 负责行为决策(Decision)与轨迹生成(Planning)
├── simple_control/    # [控制层] 根据指令或轨迹计算控制量
├── simple_visualizer/ # [交互层] 基于 WebSocket 和 Canvas 的 Web 可视化终端
└── autopilot          # [工具层] 系统管理与状态查看工具
```

## 🔄 数据流水线 (Pipeline)

系统模拟了完整的自动驾驶数据流：

1.  **Interact (交互)**: 用户在 Visualizer 前端设置目标点
2.  **Command (指令)**: Visualizer 将指令封装为 `ControlCommand` 消息发布
3.  **Map (地图)**: `simple_map` 发布静态车道线数据 `map/data`
4.  **Simulator (仿真)**: `simple_simulator` 接收控制指令，进行物理仿真并维护世界真值
    - 订阅 `control/command` (目标速度、转向角)
    - 基于单车模型 (Bicycle Model) 进行运动学积分，更新车辆位姿
    - 发布 `visualizer/data` (包含车辆真值状态、障碍物信息)
5.  **Sensor (传感器)**: `simple_sensor` 基于仿真真值生成传感器观测数据
    - 订阅 `visualizer/data` (真值)
    - 添加传感器噪声和误差，模拟真实传感器特性
    - 发布传感器观测数据
6.  **Perception (感知)**: 基于传感器数据生成障碍物位置与状态
    - 订阅传感器观测数据
    - 发布 `perception/obstacles`
7.  **Prediction (预测)**: 基于感知结果预测障碍物未来动态
8.  **Planning (规划)**:
    - **Decision (决策)**: 判断行为意图（如避让、停车、绕行）
    - **Motion Planning (运动规划)**: 结合**Map 数据**和目标点，生成三阶贝塞尔曲线轨迹
    - 发布 `planning/trajectory`
9.  **Control (控制)**:
    - 接收 `planning/trajectory` 或直接响应控制指令
    - 计算控制量（目标速度、转向角）
    - 发布 `control/command` 给 Simulator
10. **Visualize (显示)**: Visualizer 订阅数据并渲染到浏览器（含地图车道线）

## 🚀 快速开始

### 1. 编译

#### 方式 A：使用 Docker 编译（推荐，跨平台）

使用 Docker 可以确保跨平台一致性，无需手动安装依赖：

```bash
# 构建 Docker 镜像（首次使用或依赖更新时需要）
./build_docker.sh build

# 在容器中编译整个项目
./build_docker.sh compile

# 或者使用 docker-compose
docker-compose up builder

# 只编译指定模块
./build_docker.sh module simple_control

# 进入容器进行交互式调试
./build_docker.sh shell
```

**Docker 方式的优势：**

- ✅ 跨平台支持（Windows、macOS、Linux）
- ✅ 环境一致性，避免依赖问题
- ✅ 自动处理所有依赖（CMake、Protobuf 等）
- ✅ 构建产物保留在主机上，可直接使用
- ✅ 支持增量编译，Docker 层缓存加速构建

**Docker 使用技巧：**

```bash
# 开发模式（包含调试工具）
DOCKERFILE=Dockerfile.dev ./build_docker.sh build
DOCKERFILE=Dockerfile.dev ./build_docker.sh shell

# 使用自定义镜像标签
IMAGE_TAG=v1.0 ./build_docker.sh build

# 查看帮助
./build_docker.sh help
```

#### 方式 B：本地编译

项目提供了一键编译脚本，会自动处理所有模块的依赖关系：

```bash
cd simple-autopilot
./build_all.sh
```

**本地编译要求：**

- CMake (>= 3.10)
- C++17 编译器
- Protobuf（脚本会自动安装）
- Pthread

### 2. 运行

#### 方式 A：带终端窗口启动（推荐调试用）

```bash
chmod +x run_all.sh
./run_all.sh
```

此脚本会自动打开多个终端窗口，分别显示各模块的实时输出。

#### 方式 B：后台静默运行

```bash
chmod +x run_headless.sh
./run_headless.sh
```

此脚本在当前终端后台运行所有核心模块，日志保存在 `logs/` 目录下。按 `Ctrl+C` 即可一键停止所有进程。

#### 方式 C：手动分步运行

如果你无法使用上述脚本，请手动开启多个终端窗口，分别运行以下组件：

**终端 1: 系统守护进程 (Daemon)**
负责管理各模块进程，并收集 CPU、内存等运行指标。

```bash
./simple_daemon/build/daemon_node
```

**终端 2: 系统监视器 (System Monitor)**
用于实时监控各节点健康状态（由 Daemon 报告）、网络流量以及业务指标。

```bash
./autopilot status
```

**终端 3: 地图模块 (Map)**
提供静态地图数据。

```bash
./simple_map/build/map_node
```

**终端 4: 仿真引擎 (Simulator)**
物理仿真引擎，维护世界真值。

```bash
./simple_simulator/build/simulator_node
```

**终端 5: 传感器模块 (Sensor)**
基于仿真真值生成传感器观测数据。

```bash
./simple_sensor/build/sensor_node
```

**终端 6: 感知模块 (Perception)**
负责障碍物生成与检测。

```bash
./simple_perception/build/perception_node
```

**终端 7: 规划模块 (Planning)**
负责路径生成和决策逻辑。

```bash
./simple_planning/build/planning_node
```

**终端 8: 控制模块 (Control)**
计算控制量（目标速度、转向角）。

```bash
./simple_control/build/control_server
```

**终端 9: 可视化模块 (Visualizer)**
Web 服务器，负责将数据推送到浏览器。

```bash
./simple_visualizer/build/server
```

### 3. 停止运行

无论使用哪种方式启动，你都可以使用以下脚本一键停止所有相关进程：

```bash
chmod +x stop_all.sh
./stop_all.sh
```

### 4. 体验

1.  打开浏览器访问 `http://localhost:8080`
2.  你将看到一个包含车辆、障碍物和网格的实时视图。
3.  **自动驾驶演示**：
    - 点击 **"Go to Target (30,10)"** 按钮。
    - 观察屏幕上生成的 **青色轨迹线** (由 Planning 生成)。
    - 观察车辆如何沿轨迹行驶 (由 Control 执行)。
4.  **紧急停车**：点击 "Emergency Stop" 按钮，车辆将立即停止。

## 🧩 模块详解

### Common Msgs

- 存放 `.proto` 文件，定义核心数据结构：
  - `FrameData`: 包含车辆状态、障碍物、规划轨迹、电量等。
  - `ControlCommand`: 定义控制指令格式。
- 编译生成独立的动态库，供各模块链接使用。

### Simple Daemon

- **功能**：作为系统的守护进程，负责监控各业务节点的生命周期（启动/停止/状态检测）。
- **进程监控**：实时采集各节点的 PID、CPU 使用率和内存占用。
- **远程控制**：响应来自 Visualizer 的指令，实现节点的远程拉起或关闭。

### Simple Map

- **功能**：提供地图静态车道线等数据。
- **数据流**：发布 `map/data`。

### Simple Simulator

- **功能**：物理仿真引擎，扮演"虚拟物理世界"的角色，维护世界真值（Ground Truth）并进行物理仿真。
- **核心职责**：
  - 接收 `control/command` (目标速度、转向角)
  - 基于单车模型 (Bicycle Model) 进行运动学积分，更新车辆位姿
  - 维护静态和动态障碍物的真值状态
  - 发布 `visualizer/data` (包含车辆真值状态、障碍物信息)
- **算法**：
  - 运行频率：100Hz
  - 动力学模拟：使用一阶滞后模型模拟车辆加速过程
  - 运动学模拟：使用 Bicycle Model 进行积分
- **架构意义**：实现了 Software-in-the-Loop (SIL) 架构，使得核心算法模块可以在不修改代码的情况下从仿真迁移到实车。

### Simple Sensor

- **功能**：基于仿真真值生成传感器观测数据，模拟真实传感器的特性。
- **数据流**：
  - 订阅 `visualizer/data` (来自 Simulator 的真值)
  - 添加传感器噪声和误差
  - 发布传感器观测数据
- **作用**：在仿真环境中模拟真实传感器的观测特性，为感知模块提供更真实的输入数据。

### Simple Planning

- **功能**：接收前端指令，结合车辆当前状态，规划未来路径。
- **算法**：实现了三阶贝塞尔曲线生成算法，确保轨迹平滑且符合车头朝向约束。
- **依赖**：引入了 `json11` 库用于解析 JSON 指令。

### Simple Perception

- **功能**：基于传感器观测数据生成障碍物位置与状态。
- **数据流**：
  - 订阅传感器观测数据（来自 `simple_sensor`）
  - 进行障碍物检测与跟踪
  - 发布 `perception/obstacles`
- **算法**：内置简单的障碍物检测和跟踪算法。

### Simple Control

- **功能**：根据规划轨迹或控制指令计算控制量（目标速度、转向角）。
- **算法**：内置 Pure Pursuit (纯追踪) 算法。
- **数据流**：
  - 订阅 `planning/trajectory` 或直接响应控制指令
  - 订阅 `visualizer/data` (车辆当前状态，用于反馈控制)
  - 发布 `control/command` (目标速度、转向角) 给 Simulator
- **架构说明**：Control 模块不直接维护车辆位置，只负责计算控制量。车辆的实际运动由 Simulator 通过物理仿真计算得出。

### System Monitor

- **实时监视**：提供三合一的监控界面，包括车辆仪表盘、节点健康状态面板和网络流量统计。
- **状态同步**：通过订阅 `system/status` 主题，实时显示由 Daemon 汇报的各模块运行指标。

### Simple Visualizer

- **后端**：使用 CivetWeb 搭建 HTTP/WebSocket 服务器。
- **前端**：HTML5 Canvas 绘制，支持：
  - 实时车辆位姿渲染
  - 规划轨迹渲染 (青色曲线)
  - 历史轨迹渲染
  - 交互式指令发送

## 🛠️ 依赖项

- CMake (>= 3.10)
- C++17 编译器
- Protobuf (Google Protocol Buffers)
- Pthread
- (内置) CivetWeb, json11

## 📝 学习要点

通过这个项目，你可以学习到：

1.  **架构设计**：如何设计去中心化的 Pub/Sub 架构，以及 Planning/Control 分层设计。
2.  **进程管理**：学习如何编写 Daemon 进程来管理复杂软件栈的生命周期。
3.  **构建系统**：CMake 多模块管理，第三方库集成 (json11, protobuf)。
4.  **算法实现**：贝塞尔曲线规划、Pure Pursuit 控制、运动学建模。
5.  **全栈开发**：从底层 C++ 消息定义到前端可视化渲染的完整链路。
