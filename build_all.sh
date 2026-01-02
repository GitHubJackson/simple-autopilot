#!/bin/bash
set -e

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# 统一安装目录
INSTALL_DIR="$SCRIPT_DIR/install"

echo "=== 0. 清理旧构建 ==="
"$SCRIPT_DIR/scripts/clean.sh"

echo "=== 0.5 检查并安装依赖 ==="
PROTOC_PATH="$INSTALL_DIR/bin/protoc"
if [ ! -f "$PROTOC_PATH" ]; then
    echo "Protobuf 未找到，正在自动安装..."
    "$SCRIPT_DIR/scripts/install_protobuf.sh"
    if [ $? -ne 0 ]; then
        echo "Protobuf 安装失败！"
        exit 1
    fi
else
    echo "Protobuf 已安装: $PROTOC_PATH"
fi

echo "=== 1. 编译并安装 Common Msgs (公共消息库) ==="
mkdir -p common_msgs/build
cd common_msgs/build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ..
make -j4
make install
cd ../..

echo "=== 2. 编译并安装 Simple Middleware (中间件) ==="
mkdir -p simple_middleware/build
cd simple_middleware/build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ..
make -j4
make install
cd ../..

echo "=== 2.5 编译 Daemon 模块 ==="
mkdir -p simple_daemon/build
cd simple_daemon/build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ..
make -j4
cd ../..

echo "=== 2.55 编译 Simulator 模块 ==="
mkdir -p simple_simulator/build
cd simple_simulator/build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ..
make -j4
cd ../..

echo "=== 2.6 编译 Sensor 模块 ==="
mkdir -p simple_sensor/build
cd simple_sensor/build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ..
make -j4
cd ../..

echo "=== 3. 编译 System Monitor ==="
mkdir -p system_monitor/build
cd system_monitor/build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ..
make -j4
make install
cd ../..

echo "=== 4. 编译 Map 模块 ==="
mkdir -p simple_map/build
cd simple_map/build
cmake ..
make -j4
if [ $? -eq 0 ]; then
    echo "Map 模块编译成功！"
else
    echo "Map 模块编译失败！"
    exit 1
fi
cd ../..

echo "=== 5. 编译 Planning 模块 ==="
mkdir -p simple_planning/build
cd simple_planning/build
cmake ..
make -j4
if [ $? -eq 0 ]; then
    echo "Planning 模块编译成功！"
else
    echo "Planning 模块编译失败！"
    exit 1
fi
cd ../..

echo "=== 5.5 编译 Perception 模块 ==="
mkdir -p simple_perception/build
cd simple_perception/build
cmake ..
make -j4
if [ $? -eq 0 ]; then
    echo "Perception 模块编译成功！"
else
    echo "Perception 模块编译失败！"
    exit 1
fi
cd ../..

echo "=== 5.6 编译 Prediction 模块 ==="
mkdir -p simple_prediction/build
cd simple_prediction/build
cmake ..
make -j4
if [ $? -eq 0 ]; then
    echo "Prediction 模块编译成功！"
else
    echo "Prediction 模块编译失败！"
    exit 1
fi
cd ../..

echo "=== 6. 编译 Control 模块 ==="
mkdir -p simple_control/build
cd simple_control/build
cmake ..
make -j4
cd ../..

echo "=== 7. 编译 Visualizer 模块 ==="
mkdir -p simple_visualizer/build
cd simple_visualizer/build
cmake ..
make -j4
cd ../..

echo ""
echo "=============================================="
echo "   全栈编译完成！"
echo "=============================================="
