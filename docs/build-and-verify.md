# 构建与验证指南

本文档详细说明如何构建、测试和验证 iov-vehicle-ccu-diag 项目。

## 构建环境要求

### 必需工具

| 工具 | 版本要求 | 说明 |
|------|----------|------|
| CMake | >= 3.24 | 跨平台构建系统 |
| C++ 编译器 | 支持 C++17 | GCC >= 7, Clang >= 5, Apple Clang >= 10 |
| Make | 任意版本 | 构建工具 |

### 必需依赖库

| 库 | 用途 | 安装方式 |
|----|------|----------|
| yaml-cpp | YAML 配置解析 | `brew install yaml-cpp` 或 `apt install libyaml-cpp-dev` |
| nlohmann_json | JSON 数据处理 | `brew install nlohmann-json` 或 `apt install nlohmann-json3-dev` |
| CCUFramework | 已安装 Framework SDK | Framework 仓库 install 到 `$HOME/.local` 等前缀 |
| Google Test | 单元测试框架（仅 `BUILD_TESTING=ON`） | `brew install googletest` 或 `apt install libgtest-dev` |
| vsomeip3 / Boost | 真实 SOME/IP 后端（`USE_MOCK_SOMEIP=OFF`） | 由 CCU-BUILD 受控 recipe/sysroot 提供 |

## 构建步骤

### 方式一：使用构建脚本（推荐）

项目提供了自动化构建脚本 `scripts/build.sh`，支持以下选项：

```bash
# 完整构建（包含测试）
./scripts/build.sh

# 仅构建库和可执行文件，跳过测试
./scripts/build.sh --no-test

# 清理构建目录后重新构建
./scripts/build.sh --clean

# 构建完成后安装到系统目录
./scripts/build.sh --install

# 组合使用
./scripts/build.sh --clean --install
```

#### 构建脚本功能

- 自动检测并安装缺失的依赖（支持 macOS 和 Linux）
- 配置 CMake 项目
- 编译源代码
- 运行单元测试（除非指定 `--no-test`）
- 可选安装到系统目录

### 方式二：手动构建

#### 1. 安装依赖

**macOS (Homebrew):**

```bash
brew install cmake yaml-cpp nlohmann-json googletest
```

**Ubuntu/Debian:**

```bash
sudo apt-get update
sudo apt-get install -y cmake libyaml-cpp-dev nlohmann-json3-dev libgtest-dev
```

**CentOS/RHEL:**

```bash
sudo yum install -y cmake3 yaml-cpp-devel json-devel gtest-devel
```

#### 2. 创建构建目录

```bash
mkdir -p build
cd build
```

#### 3. 配置项目

开发/CI（显式启用 mock SOME/IP 与测试）：

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DUSE_MOCK_SOMEIP=ON \
  -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH="$HOME/.local"
```

生产 TARGET 由 CCU-BUILD 显式传入：

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DUSE_MOCK_SOMEIP=OFF \
  -DBUILD_TESTING=OFF \
  -DCMAKE_PREFIX_PATH=<受控 SDK/dependency staging>
```

依赖库安装在非标准路径时，用 `-DCMAKE_PREFIX_PATH=<prefix>` 指定。

#### 4. 编译

```bash
cmake --build . -j$(nproc)
```

#### 5. 运行测试

```bash
# 使用 CTest 运行所有测试
ctest --output-on-failure

# 或直接运行测试可执行文件
./CcuDiagTests
```

## 构建产物

成功构建后，将在 `build/` 目录下生成以下文件：

| 文件 | 说明 |
|------|------|
| `ccu_diag` | 主程序可执行文件 |
| `libccu_diag_lib.a` | 静态库文件 |
| `CcuDiagTests` | 测试可执行文件（仅 `BUILD_TESTING=ON`） |
| `test_ccu_sec_client_proxy` | SEC client 契约测试（仅 `BUILD_TESTING=ON`） |

## 验证步骤

### 1. 验证构建产物

```bash
# 检查可执行文件是否生成
ls -la build/ccu_diag

# 检查库文件是否生成
ls -la build/libccu_diag_lib.a

# 检查测试文件是否生成
ls -la build/CcuDiagTests
```

### 2. 验证程序功能

```bash
# 显示帮助信息
./build/ccu_diag --help

# 显示版本信息
./build/ccu_diag --version

# 使用开发夹具启动（统一配置 + 公共配置）
./build/ccu_diag --diag-config config/dev/diag.yaml \
  --common-config config/dev/common.yaml --someip-port 0
```

### 3. 运行单元测试

```bash
cd build
ctest --output-on-failure
```

预期输出：

```
Test project /path/to/build
    Start 1: CcuDiagTests
1/1 Test #1: CcuDiagTests ....................   Passed    X.XX sec

100% tests passed, 0 tests failed out of 1

Total Test time (real) =   X.XX sec
```

### 4. 运行特定测试套件

```bash
# 运行所有测试
./build/CcuDiagTests

# 运行特定测试套件
./build/CcuDiagTests --gtest_filter="*ConfigLoader*"

# 运行特定测试用例
./build/CcuDiagTests --gtest_filter="ConfigLoaderTest.LoadDiagConfig"
```

### 5. 验证配置文件

```bash
# 校验默认模板与开发夹具（schema + 秘密/VIN/application 一致性）
python3 ci/verify-config-schema.py \
  config/diag.default.yaml config/dev/diag.yaml

# 负例必须被拒绝
python3 ci/verify-config-schema.py tests/config/invalid_top_level_common.yaml
```

### 6. 安装与 CI 门禁

```bash
# 仅安装 ccu-diag-runtime（DESTDIR 由 BUILD 管理）
DESTDIR=/tmp/diag-root cmake --install build \
  --prefix /usr --component ccu-diag-runtime

# 全套 HOST/commit 门禁（CMake lint、unit、schema、install、smoke）
./ci/run-ci-gates.sh
```

## 常见问题

### 1. CMake 版本过低

**错误信息：**
```
CMake Error at CMakeLists.txt:1 (cmake_minimum_required):
  CMake 3.24 or higher is required.
```

**解决方案：**
- macOS: `brew upgrade cmake`
- Ubuntu: 使用官方 PPA 安装最新版本

### 2. 找不到依赖库

**错误信息：**
```
Could not find a package configuration file provided by "yaml-cpp"
```

**解决方案：**
- 确保已安装依赖库
- 使用 `cmake -DCMAKE_PREFIX_PATH=<path>` 指定库路径

### 3. 编译错误

**错误信息：**
```
error: 'xxx' is not a member of 'std'
```

**解决方案：**
- 确保编译器支持 C++17
- 检查 CMakeLists.txt 中的 C++ 标准设置

### 4. 测试失败

**错误信息：**
```
[  FAILED  ] TestSuite.TestCase
```

**解决方案：**
- 查看详细错误信息：`./build/CcuDiagTests --gtest_print_time=0`
- 检查测试环境是否正确配置

## 清理构建

```bash
# 删除构建目录
rm -rf build/

# 或使用构建脚本清理
./scripts/build.sh --clean
```

## IDE 集成

### CLion

1. 打开项目目录
2. CMake 会自动配置
3. 使用内置的构建和运行功能

### VS Code

1. 安装 C/C++ 和 CMake Tools 扩展
2. 打开项目目录
3. 使用 CMake Tools 进行配置和构建

### Xcode (macOS)

```bash
# 生成 Xcode 项目
cmake -G Xcode ..

# 打开项目
open CcuDiagService.xcodeproj
```

## 性能优化

### 并行编译

使用 `-j` 选项指定并行编译的核心数：

```bash
make -j$(nproc)  # Linux
make -j$(sysctl -n hw.ncpu)  # macOS
```

### 增量编译

修改代码后，只需运行：

```bash
cd build && make
```

CMake 会自动检测变更并仅重新编译必要的文件。

## 调试

### 启用调试符号

构建脚本默认启用调试符号（`-g` 选项），可直接使用 GDB 或 LLDB 调试：

```bash
# 使用 GDB 调试
gdb ./build/ccu_diag

# 使用 LLDB 调试 (macOS)
lldb ./build/ccu_diag
```

### 内存检查

使用 Valgrind 检查内存问题（Linux）：

```bash
valgrind --leak-check=full ./build/CcuDiagTests
```

## 持续集成

项目可集成到 CI/CD 流程中：

```yaml
# 本地/CI 门禁（推荐）
./ci/run-ci-gates.sh

# GitHub Actions 示例
name: Build and Test
on: [push, pull_request]
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake libyaml-cpp-dev nlohmann-json3-dev libgtest-dev
      - name: Build
        run: ./scripts/build.sh
```

## 相关文档

- [README.md](../README.md) - 项目概述
- [LICENSE](../LICENSE) - 许可证信息