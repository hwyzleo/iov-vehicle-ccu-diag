# iov-vehicle-cgw-diag

车联网车端中央网关（CGW）诊断服务模块

## 项目概述

本项目实现了车联网车端中央网关的诊断业务功能，包括：

- **UDS 诊断客户端**：实现统一诊断服务（UDS）协议通信
- **DID 注册表管理**：管理诊断标识符（DID）配置
- **诊断路由**：支持 DoCAN 和 DoIP 双传输协议
- **版本信息采集**：采集车辆 ECU 软件版本信息
- **FOTA 接口**：提供固件空中升级的诊断支持

## 目录结构

```
iov-vehicle-cgw-diag/
├── CMakeLists.txt          # CMake 构建配置
├── README.md               # 项目说明文档
├── LICENSE                 # MIT 许可证
├── config/                 # 配置文件目录
│   ├── diag_config.yaml    # 诊断参数配置
│   └── did_registry.yaml   # DID 注册表配置
├── include/                # 头文件目录
│   ├── config_loader.h     # 配置加载器
│   ├── constants.h         # 常量定义
│   ├── data_models.h       # 数据模型
│   ├── diagnostic_router.h # 诊断路由器
│   ├── did_registry.h      # DID 注册表
│   ├── do_can_transport.h  # DoCAN 传输层
│   ├── doip_transport.h    # DoIP 传输层
│   ├── error_codes.h       # 错误码定义
│   ├── fota_facade.h       # FOTA 接口
│   ├── transport_adapter.h # 传输适配器接口
│   ├── uds_client.h        # UDS 客户端
│   └── version_collector.h # 版本采集器
├── src/                    # 源代码目录
│   ├── main.cpp            # 主程序入口
│   └── *.cpp               # 各模块实现
├── tests/                  # 测试代码目录
│   ├── test_*.cpp          # 单元测试
│   └── mock_transport.h    # 测试模拟
├── docs/                   # 文档目录
│   └── build-and-verify.md # 构建与验证指南
└── scripts/                # 脚本目录
    └── build.sh            # 构建脚本
```

## 构建要求

### 依赖项

- **CMake** >= 3.10
- **C++17** 编译器
- **yaml-cpp**：YAML 配置文件解析
- **nlohmann_json**：JSON 数据处理
- **Google Test**：单元测试框架

### 安装依赖

#### macOS (Homebrew)

```bash
brew install cmake yaml-cpp nlohmann-json googletest
```

#### Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install cmake libyaml-cpp-dev nlohmann-json3-dev libgtest-dev
```

## 快速开始

### 使用构建脚本（推荐）

```bash
# 完整构建（包含测试）
./scripts/build.sh

# 仅构建库和可执行文件
./scripts/build.sh --no-test

# 清理后重新构建
./scripts/build.sh --clean

# 安装到系统目录
./scripts/build.sh --install
```

### 手动构建

```bash
# 创建构建目录
mkdir build && cd build

# 配置项目
cmake ..

# 编译
make -j$(nproc)

# 运行测试
ctest --output-on-failure
```

## 使用方法

### 命令行选项

```bash
cgw_diag [选项]

选项：
  --vin <VIN>              采集车辆软件清单
  --ecu <ECU_ID>           获取指定 ECU 版本
  --registry-version       打印注册表版本
  --version                打印应用版本
  --did-registry <path>    DID 注册表 YAML 路径
  --diag-config <path>     诊断配置 YAML 路径
  --can-interface <name>   CAN 接口名称（默认：can0）
  --can-baudrate <rate>    CAN 波特率（默认：500000）
  --doip-ip <ip>           DoIP 服务器 IP 地址（默认：192.168.1.1）
  --doip-port <port>       DoIP 服务器端口（默认：13400）
  --help                   显示帮助信息
```

### 示例

```bash
# 采集车辆软件清单
./build/cgw_diag --vin LVSHFFAN5KF000001

# 获取指定 ECU 版本
./build/cgw_diag --ecu BMS

# 打印注册表版本
./build/cgw_diag --registry-version

# 使用自定义配置
./build/cgw_diag --vin LVSHFFAN5KF000001 \
  --did-registry config/did_registry.yaml \
  --diag-config config/diag_config.yaml \
  --can-interface can1 \
  --doip-ip 192.168.1.100
```

### 输出格式

程序输出 JSON 格式的诊断结果：

```json
{
  "vin": "LVSHFFAN5KF000001",
  "registry_version": "2026.07.0",
  "overall_result": "ALL_OK",
  "ecu_list": [
    {
      "ecu_id": "BMS",
      "part_number": "12345678901",
      "sw_version": "1.0.0",
      "source": "UDS_0x22",
      "status": "OK"
    }
  ]
}
```

## 配置说明

### DID 注册表配置 (did_registry.yaml)

```yaml
registryVersion: "2026.07.0"
baselineDids: [0xF195, 0xF188, 0xF187, 0xF193]
ecus:
  - ecuId: "BMS"
    logicalAddress: 0x1A
    transport: "DoCAN"
    readMethod: "UDS_0x22"
    securityRequired: false
    dids:
      - did: 0xF195
        field: "swVersion"
        encoding: "ASCII"
        length: 16
```

### 诊断参数配置 (diag_config.yaml)

```yaml
timing:
  p2_server_max_ms: 50
  p2_star_server_max_ms: 5000
  s3_session_timeout_ms: 5000
  tester_present_interval_ms: 2000
  request_timeout_ms: 2000
  retry_interval_ms: 100
retry:
  max_count: 2
```

## 测试

### 运行测试

```bash
# 运行所有测试
cd build && ctest --output-on-failure

# 或者直接运行测试可执行文件
./build/CgwDiagTests

# 运行特定测试
./build/CgwDiagTests --gtest_filter="*ConfigLoader*"
```

### 测试覆盖率

项目包含完整的单元测试和集成测试，覆盖以下模块：

- 错误码处理
- 数据模型
- 配置加载器
- DID 注册表
- UDS 客户端
- 诊断路由器
- 传输适配器
- 版本采集器
- FOTA 接口

## 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件

## 贡献指南

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 创建 Pull Request

## SOME/IP 服务

CGW-DIAG 现在作为独立进程运行，通过 SOME/IP 提供服务间通信能力。

### 服务配置

SOME/IP 配置文件位于 `config/someip_config.yaml`。

### 命令行参数

```
./cgw_diag [options]
Options:
  --did-registry <path>    Path to DID registry YAML
  --diag-config <path>     Path to diag config YAML
  --someip-config <path>   Path to SOME/IP config YAML
  --can-interface <name>   CAN interface name
  --can-baudrate <rate>    CAN baudrate
  --doip-ip <ip>           DoIP server IP address
  --doip-port <port>       DoIP server port
  --someip-ip <ip>         SOME/IP listen address
  --someip-port <port>     SOME/IP listen port
  --version                Print application version
  --help                   Show this help message
```

### SOME/IP 方法

| 方法 | Method ID | 说明 |
|------|-----------|------|
| collectVehicleInventory | 0x0001 | 触发一轮采集，返回 VehicleSoftwareSnapshot |
| getEcuVersion | 0x0002 | 按单 ECU 读取版本 |
| getRegistryVersion | 0x0003 | 返回当前 DidRegistry 版本 |

## 联系方式

- 项目维护者：hwyz_leo
- 项目链接：https://github.com/hwyz_leo/iov-vehicle-cgw-diag