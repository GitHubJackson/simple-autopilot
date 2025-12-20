#!/bin/bash
set -e

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# 统一安装目录
INSTALL_DIR="$SCRIPT_DIR/install"

echo "=== 1. 编译并安装 Simple Middleware (中间件) ==="
mkdir -p simple_middleware/build
cd simple_middleware/build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ..
make -j4
make install
if [ $? -eq 0 ]; then
    echo "中间件安装成功 -> $INSTALL_DIR"
else
    echo "中间件编译失败！"
    exit 1
fi
cd ../..

echo "=== 2. 编译并安装 Common Msgs (公共消息库) ==="
mkdir -p common_msgs/build
cd common_msgs/build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" ..
make -j4
make install
if [ $? -eq 0 ]; then
    echo "Common Msgs 安装成功 -> $INSTALL_DIR"
else
    echo "Common Msgs 编译失败！"
    exit 1
fi
cd ../..

echo "=== 3. 编译 Control 模块 (依赖中间件 & Common Msgs) ==="
mkdir -p simple_control/build
cd simple_control/build
cmake ..
make -j4
if [ $? -eq 0 ]; then
    echo "Control 模块编译成功！"
else
    echo "Control 模块编译失败！"
    exit 1
fi
cd ../..

echo "=== 4. 编译 Visualizer 模块 (依赖中间件 & Common Msgs) ==="
mkdir -p simple_visualizer/build
cd simple_visualizer/build
cmake ..
make -j4
if [ $? -eq 0 ]; then
    echo "Visualizer 模块编译成功！"
else
    echo "Visualizer 模块编译失败！"
    exit 1
fi
cd ../..

echo ""
echo "=============================================="
echo "   编译完成！请在三个不同的终端中分别运行："
echo "=============================================="
echo "1. [中间件监视器] (中间件工具)"
echo "   $INSTALL_DIR/bin/middleware_monitor"
echo ""
echo "2. [控制模块] (生产者)"
echo "   $SCRIPT_DIR/simple_control/build/control_server"
echo ""
echo "3. [可视化模块] (消费者)"
echo "   $SCRIPT_DIR/simple_visualizer/build/server"
echo "=============================================="
