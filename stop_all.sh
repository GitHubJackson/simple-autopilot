#!/bin/bash

# 获取项目根目录绝对路径
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

echo "=== 正在停止 AutoPilot 系统所有进程 ==="

# 定义要查找并终止的进程特征（匹配命令行包含的字符串）
# 使用相对路径片段以提高匹配准确性，防止误杀其他名为 server 的进程
TARGETS=(
    "simple_perception/build/perception_node"
    "simple_control/build/control_server"
    "simple_planning/build/planning_node"
    "simple_visualizer/build/server"
    "system_monitor/build/monitor"
    "simple_daemon/build/daemon_node"
    "simple_simulator/build/bin/simulator_node"
    "simple_sensor/build/sensor_node"
    "simple_map/build/map_server"
    "autopilot status"  # 匹配 ./autopilot status 命令
)

for target in "${TARGETS[@]}"; do
    # 检查进程是否存在
    if pgrep -f "$target" > /dev/null; then
        echo "正在终止: $target"
        # 杀掉进程
        pkill -f "$target"
    else
        echo "未发现运行中的: $target"
    fi
done

echo "=== 所有相关进程已清理 ==="

