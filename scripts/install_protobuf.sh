#!/bin/bash
set -e

# 获取脚本所在目录的上一级目录，即项目根目录 (simple-autopilot)
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="${SCRIPT_DIR}/.."
BUILD_DIR="${PROJECT_ROOT}/build/protobuf_build"
INSTALL_DIR="${PROJECT_ROOT}/install"

# Protobuf 版本
PROTOBUF_VERSION="v3.19.4"
PROTOBUF_REPO="https://github.com/protocolbuffers/protobuf.git"

echo "=== Installing Protobuf ${PROTOBUF_VERSION} ==="
echo "Project Root: ${PROJECT_ROOT}"
echo "Install Dir:  ${INSTALL_DIR}"

# 创建构建目录
mkdir -p ${BUILD_DIR}

# 检查是否需要克隆或更新
if [ ! -d "${BUILD_DIR}/protobuf_src" ]; then
    echo "Cloning protobuf..."
    git clone --depth 1 --branch ${PROTOBUF_VERSION} --recursive ${PROTOBUF_REPO} ${BUILD_DIR}/protobuf_src
else
    echo "Protobuf source already exists."
fi

# 编译和安装
cd ${BUILD_DIR}/protobuf_src

echo "Configuring CMake..."
# 注意：对于 Protobuf v3.x 版本，CMakeLists.txt 位于 cmake/ 子目录下
# 添加 -DCMAKE_POLICY_VERSION_MINIMUM=3.5 以解决新版 CMake 不兼容旧版声明的问题
cmake -S cmake -B build_release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=17 \
    -Dprotobuf_BUILD_TESTS=OFF \
    -Dprotobuf_BUILD_EXAMPLES=OFF \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 

echo "Building..."
cmake --build build_release -j$(nproc)

echo "Installing..."
cmake --install build_release

echo "=== Protobuf installed successfully to ${INSTALL_DIR} ==="
