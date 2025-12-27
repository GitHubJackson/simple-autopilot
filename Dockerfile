# 使用 Ubuntu 22.04 作为基础镜像（LTS 版本，稳定且支持 C++17）
FROM ubuntu:22.04

# 设置环境变量，避免交互式安装
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# 设置工作目录
WORKDIR /workspace

# 安装基础构建工具和依赖（合并 RUN 命令以减少层数）
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    curl \
    pkg-config \
    ninja-build \
    autoconf \
    automake \
    libtool \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# 定义构建参数（可选，用于自定义）
ARG BUILD_TYPE=Release
ARG JOBS=4

# 设置环境变量
ENV INSTALL_DIR=/workspace/install
ENV PATH="${INSTALL_DIR}/bin:${PATH}"
ENV LD_LIBRARY_PATH="${INSTALL_DIR}/lib:${LD_LIBRARY_PATH}"
ENV CMAKE_BUILD_PARALLEL_LEVEL=${JOBS}

# 先复制脚本文件（利用 Docker 缓存层）
COPY scripts/ /workspace/scripts/
RUN chmod +x /workspace/scripts/*.sh

# 创建 install 目录
RUN mkdir -p /workspace/install

# 复制构建脚本
COPY build_all.sh /workspace/build_all.sh
RUN chmod +x /workspace/build_all.sh

# 复制项目文件（放在最后，这样代码修改不会影响依赖安装的缓存）
COPY . /workspace/

# 默认执行构建（可以通过 docker run 覆盖）
CMD ["/workspace/build_all.sh"]

