# 构建与验证指南

本文档详细说明如何构建、测试和验证 iov-vehicle-ccu-diag 项目。

## 构建环境要求

### 必需工具

| 工具 | 版本要求 | 说明 |
|------|----------|------|
| CMake | >= 3.10 | 跨平台构建系统 |
| C++ 编译器 | 支持 C++17 | GCC >= 7, Clang >= 5, Apple Clang >= 10 |
| Make | 任意版本 | 构建工具 |

### 必需依赖库

| 库 | 用途 | 安装方式 |
|----|------|----------|
| yaml-cpp | YAML 配置解析 | `brew install yaml-cpp` 或 `apt install libyaml-cpp-dev` |
| nlohmann_json | JSON 数据处理 | `brew install nlohmann-json` 或 `apt install nlohmann-json3-dev` |
| Google Test | 单元测试框架 | `brew install googletest` 或 `apt install libgtest-dev` |

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

```bash
cmake ..
```

如果依赖库安装在非标准路径，可以指定路径：

```bash
cmake -DCMAKE_PREFIX_PATH=/usr/local ..
```

#### 4. 编译

```bash
# 使用所有 CPU 核心
make -j$(nproc)

# 或指定核心数
make -j4
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
| `CcuDiagTests` | 测试可执行文件 |
| `compile_commands.json` | 编译命令数据库（用于 IDE） |

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

# 显示注册表版本
./build/ccu_diag --registry-version
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
# 检查配置文件语法
./build/ccu_diag --registry-version
```

如果配置文件格式正确，将输出注册表版本号。

## 常见问题

### 1. CMake 版本过低

**错误信息：**
```
CMake Error at CMakeLists.txt:1 (cmake_minimum_required):
  CMake 3.10 or higher is required.
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