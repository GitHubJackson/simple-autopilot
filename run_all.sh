#!/bin/bash

# 获取项目根目录绝对路径
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# 定义要启动的模块和对应的命令
MODULES=(
    "Daemon:./simple_daemon/build/daemon_node"
    "System Monitor:./system_monitor/build/monitor"
    "Map:./simple_map/build/map_server"
    "Simulator:./simple_simulator/build/bin/simulator_node"
    "Sensor:./simple_sensor/build/sensor_node"
    "Perception:./simple_perception/build/perception_node"
    "Planning:./simple_planning/build/planning_node"
    "Control:./simple_control/build/control_server"
    "Visualizer:./simple_visualizer/build/server"
)

echo "=== 正在启动 AutoPilot 演示系统 ==="
echo "项目路径: $DIR"

# 检测操作系统
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS 实现
    APP_NAME="Terminal"
    
    # 安全转义目录路径
    SAFE_DIR=$(printf %q "$DIR")
    
    for module in "${MODULES[@]}"; do
        TITLE="${module%%:*}"
        CMD="${module#*:}"
        
        echo "启动 $TITLE ..."
        
        # 构造 Shell 命令
        # 使用 printf 设置标题更通用
        SHELL_CMD="cd $SAFE_DIR; printf '\\033]0;%s\\007' '$TITLE'; echo '=== Starting $TITLE ==='; $CMD; echo '=== Process Exited ==='; exec bash"
        
        # 转义 Shell 命令以供 AppleScript 使用
        # 1. 反斜杠 \ -> \\
        # 2. 双引号 " -> \"
        ESCAPED_CMD=$(echo "$SHELL_CMD" | sed 's/\\/\\\\/g' | sed 's/"/\\"/g')
        
        # 执行 AppleScript
        osascript <<EOF
tell application "$APP_NAME"
    do script "$ESCAPED_CMD"
end tell
EOF
    done
    
    # 激活终端应用
    osascript -e "tell application \"$APP_NAME\" to activate"
    
    echo "所有模块已启动，请检查 Terminal 窗口。"

elif command -v gnome-terminal &> /dev/null; then
    # Linux (Gnome Terminal) 实现
    for module in "${MODULES[@]}"; do
        TITLE="${module%%:*}"
        CMD="${module#*:}"
        
        gnome-terminal --tab --title="$TITLE" -- bash -c "cd \"$DIR\"; echo \"=== Starting $TITLE ===\"; $CMD; exec bash"
    done

else
    echo "错误: 未检测到支持的自动终端工具。"
    echo "请手动打开终端运行以下命令："
    for module in "${MODULES[@]}"; do
        CMD="${module#*:}"
        echo "  $CMD"
    done
fi
