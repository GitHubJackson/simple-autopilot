#!/bin/bash
set -e

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Docker 镜像名称
IMAGE_NAME="simple-autopilot"
IMAGE_TAG="${IMAGE_TAG:-latest}"
DOCKERFILE="${DOCKERFILE:-Dockerfile}"

# 容器名称
CONTAINER_NAME="simple-autopilot-build"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 打印带颜色的消息
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查 Docker 是否安装
check_docker() {
    if ! command -v docker &> /dev/null; then
        print_error "Docker 未安装，请先安装 Docker"
        echo "安装指南: https://docs.docker.com/get-docker/"
        exit 1
    fi
    print_info "Docker 已安装: $(docker --version)"
}

# 构建 Docker 镜像
build_image() {
    print_info "开始构建 Docker 镜像: ${IMAGE_NAME}:${IMAGE_TAG}"
    
    docker build \
        -t "${IMAGE_NAME}:${IMAGE_TAG}" \
        -f "${DOCKERFILE}" \
        .
    
    if [ $? -eq 0 ]; then
        print_info "Docker 镜像构建成功！"
    else
        print_error "Docker 镜像构建失败！"
        exit 1
    fi
}

# 在容器中构建项目
build_in_container() {
    print_info "在 Docker 容器中构建项目..."
    
    # 停止并删除旧容器（如果存在）
    docker rm -f "${CONTAINER_NAME}" 2>/dev/null || true
    
    # 运行构建
    docker run \
        --name "${CONTAINER_NAME}" \
        -v "${SCRIPT_DIR}:/workspace" \
        -w /workspace \
        "${IMAGE_NAME}:${IMAGE_TAG}" \
        /workspace/build_all.sh
    
    if [ $? -eq 0 ]; then
        print_info "项目构建成功！"
        print_info "构建产物位于: ${SCRIPT_DIR}/install 和各个模块的 build/ 目录"
    else
        print_error "项目构建失败！"
        exit 1
    fi
}

# 进入容器交互式 shell
shell_in_container() {
    print_info "进入 Docker 容器交互式 shell..."
    
    # 如果容器不存在，先创建
    if ! docker ps -a --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
        docker run -it \
            --name "${CONTAINER_NAME}" \
            -v "${SCRIPT_DIR}:/workspace" \
            -w /workspace \
            "${IMAGE_NAME}:${IMAGE_TAG}" \
            /bin/bash
    else
        docker start "${CONTAINER_NAME}" 2>/dev/null || true
        docker exec -it "${CONTAINER_NAME}" /bin/bash
    fi
}

# 只编译指定模块
build_module() {
    local module=$1
    if [ -z "$module" ]; then
        print_error "请指定要编译的模块"
        echo "用法: $0 module <模块名>"
        exit 1
    fi
    
    print_info "在 Docker 容器中编译模块: $module"
    
    # 停止并删除旧容器（如果存在）
    docker rm -f "${CONTAINER_NAME}" 2>/dev/null || true
    
    # 运行模块构建（需要先创建 build_module.sh）
    docker run \
        --name "${CONTAINER_NAME}" \
        -v "${SCRIPT_DIR}:/workspace" \
        -w /workspace \
        "${IMAGE_NAME}:${IMAGE_TAG}" \
        bash -c "if [ -f /workspace/build_module.sh ]; then /workspace/build_module.sh $module; else echo 'build_module.sh 不存在，请使用 build_all.sh'; exit 1; fi"
}

# 清理 Docker 资源
clean_docker() {
    print_warn "清理 Docker 资源..."
    
    # 停止并删除容器
    docker rm -f "${CONTAINER_NAME}" 2>/dev/null && print_info "已删除容器: ${CONTAINER_NAME}" || true
    
    # 删除镜像（可选）
    read -p "是否删除 Docker 镜像 ${IMAGE_NAME}:${IMAGE_TAG}? (y/N) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        docker rmi "${IMAGE_NAME}:${IMAGE_TAG}" 2>/dev/null && print_info "已删除镜像: ${IMAGE_NAME}:${IMAGE_TAG}" || true
    fi
}

# 显示帮助信息
show_help() {
    echo "用法: $0 <命令> [参数]"
    echo ""
    echo "命令:"
    echo "  build         构建 Docker 镜像"
    echo "  compile       在容器中编译整个项目（默认）"
    echo "  module <名称> 在容器中编译指定模块"
    echo "  shell         进入容器交互式 shell"
    echo "  clean         清理 Docker 资源（容器和镜像）"
    echo "  help          显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0 build                    # 构建 Docker 镜像"
    echo "  $0 compile                  # 编译整个项目"
    echo "  $0 module simple_control    # 只编译 control 模块"
    echo "  $0 shell                    # 进入容器进行调试"
    echo ""
    echo "环境变量:"
    echo "  IMAGE_TAG      设置 Docker 镜像标签（默认: latest）"
    echo "  DOCKERFILE     设置 Dockerfile 路径（默认: Dockerfile）"
    echo "                  使用 Dockerfile.dev 可构建开发环境"
    echo ""
    echo "开发模式:"
    echo "  DOCKERFILE=Dockerfile.dev $0 build    # 构建开发环境镜像"
    echo "  DOCKERFILE=Dockerfile.dev $0 shell    # 进入开发环境"
    echo ""
}

# 主函数
main() {
    check_docker
    
    case "${1:-compile}" in
        build)
            build_image
            ;;
        compile|build-all)
            # 如果镜像不存在，先构建
            if ! docker images --format '{{.Repository}}:{{.Tag}}' | grep -q "^${IMAGE_NAME}:${IMAGE_TAG}$"; then
                print_warn "镜像不存在，先构建镜像..."
                build_image
            fi
            build_in_container
            ;;
        module)
            # 如果镜像不存在，先构建
            if ! docker images --format '{{.Repository}}:{{.Tag}}' | grep -q "^${IMAGE_NAME}:${IMAGE_TAG}$"; then
                print_warn "镜像不存在，先构建镜像..."
                build_image
            fi
            build_module "$2"
            ;;
        shell|bash)
            # 如果镜像不存在，先构建
            if ! docker images --format '{{.Repository}}:{{.Tag}}' | grep -q "^${IMAGE_NAME}:${IMAGE_TAG}$"; then
                print_warn "镜像不存在，先构建镜像..."
                build_image
            fi
            shell_in_container
            ;;
        clean)
            clean_docker
            ;;
        help|--help|-h)
            show_help
            ;;
        *)
            print_error "未知命令: $1"
            echo ""
            show_help
            exit 1
            ;;
    esac
}

main "$@"

