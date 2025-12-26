#!/bin/bash

# 获取项目根目录绝对路径
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
LOG_DIR="$DIR/logs"

# 创建日志目录
mkdir -p "$LOG_DIR"

# 存储子进程 PID
PIDS=()

# 清理函数：脚本退出或收到 Ctrl+C 时调用
cleanup() {
    echo ""
    echo "=== 正在停止所有后台进程 ==="
    for pid in "${PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            echo "停止进程 PID: $pid"
            kill "$pid"
        fi
    done
    echo "=== 所有进程已停止 ==="
    exit 0
}

# 注册信号捕获
trap cleanup SIGINT SIGTERM

echo "=== 正在后台启动 AutoPilot 核心模块 ==="
echo "日志文件存储于: $LOG_DIR"

# 启动模块函数
start_module() {
    local name=$1
    local cmd=$2
    local log_file="$LOG_DIR/${name}.log"

    echo "启动模块: $name"
    
    # Visualizer 需要特殊的启动方式，必须进入目录，因为要读取 www 文件夹
    if [ "$name" == "visualizer" ]; then
        cd "$DIR/simple_visualizer/build"
        ./server > "$log_file" 2>&1 &
    else
        # 切换到项目根目录执行其他模块
        cd "$DIR"
        $cmd > "$log_file" 2>&1 &
    fi
    
    # 获取最近一个后台进程的 PID
    pid=$!
    PIDS+=($pid)
    echo "  -> PID: $pid | Log: logs/${name}.log"
}

# 启动各个核心模块
# 注意：System Monitor 通常是终端交互界面，不适合后台运行，故此处不启动
start_module "perception"  "./simple_perception/build/perception_node"
start_module "control"     "./simple_control/build/control_server"
start_module "planning"    "./simple_planning/build/planning_node"
start_module "visualizer"  "./simple_visualizer/build/server"
start_module "daemon"      "./simple_daemon/build/daemon_node"
start_module "simulator"   "./simple_simulator/build/bin/simulator_node"
start_module "sensor"      "./simple_sensor/build/sensor_node"

echo ""
echo "=== 系统运行中 ==="
echo "在浏览器访问 http://localhost:8082 查看效果"
echo "按 [Ctrl+C] 停止所有进程"

# 挂起脚本，等待信号
wait

