#!/bin/bash
set -e

# 获取脚本所在目录的上一级目录，即项目根目录 (simple-autopilot)
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="${SCRIPT_DIR}/.."
INSTALL_DIR="${PROJECT_ROOT}/install"

echo "=== Cleaning Build Artifacts ==="

# 清理所有模块的 build 目录
rm -rf "${PROJECT_ROOT}/common_msgs/build"
rm -rf "${PROJECT_ROOT}/simple_middleware/build"
rm -rf "${PROJECT_ROOT}/system_monitor/build"
rm -rf "${PROJECT_ROOT}/simple_map/build"
rm -rf "${PROJECT_ROOT}/simple_planning/build"
rm -rf "${PROJECT_ROOT}/simple_control/build"
rm -rf "${PROJECT_ROOT}/simple_visualizer/build"
rm -rf "${PROJECT_ROOT}/simple_daemon/build"
rm -rf "${PROJECT_ROOT}/simple_sensor/build"
rm -rf "${PROJECT_ROOT}/simple_simulator/build"
rm -rf "${PROJECT_ROOT}/simple_perception/build"

# 清理 install 目录中的项目产物 (保留第三方库如 protobuf)
echo "Cleaning installed binaries and headers (keeping 3rdparty libs)..."
rm -rf "$INSTALL_DIR/include/common_msgs"
rm -rf "$INSTALL_DIR/include/simple_middleware"
rm -rf "$INSTALL_DIR/lib/libcommon_msgs_lib*"
rm -rf "$INSTALL_DIR/lib/libsimple_middleware_lib*"
rm -rf "$INSTALL_DIR/bin/monitor"
rm -rf "$INSTALL_DIR/bin/map_server"
rm -rf "$INSTALL_DIR/bin/planning_node"
rm -rf "$INSTALL_DIR/bin/control_node"
rm -rf "$INSTALL_DIR/bin/visualizer_server"
rm -rf "$INSTALL_DIR/bin/daemon_node"
rm -rf "$INSTALL_DIR/bin/sensor_node"

# 清理日志文件（项目根目录的 logs/ 目录）
echo "Cleaning log files..."
rm -f "${PROJECT_ROOT}/logs/*.log" 2>/dev/null || true
# 也清理可能存在的各模块目录下的日志（兼容旧版本）
rm -f "${PROJECT_ROOT}/simple_control/logs/*.log" 2>/dev/null || true
rm -f "${PROJECT_ROOT}/simple_planning/logs/*.log" 2>/dev/null || true
rm -f "${PROJECT_ROOT}/simple_perception/logs/*.log" 2>/dev/null || true
rm -f "${PROJECT_ROOT}/simple_simulator/logs/*.log" 2>/dev/null || true
rm -f "${PROJECT_ROOT}/simple_sensor/logs/*.log" 2>/dev/null || true
rm -f "${PROJECT_ROOT}/simple_visualizer/logs/*.log" 2>/dev/null || true
rm -f "${PROJECT_ROOT}/simple_daemon/logs/*.log" 2>/dev/null || true
rm -f "${PROJECT_ROOT}/simple_map/logs/*.log" 2>/dev/null || true
rm -f "${PROJECT_ROOT}/system_monitor/logs/*.log" 2>/dev/null || true
rm -f "${PROJECT_ROOT}/simple_middleware/logs/*.log" 2>/dev/null || true

echo "=== Clean Complete ==="

