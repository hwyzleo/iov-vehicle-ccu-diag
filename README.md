# iov-vehicle-ccu-diag

车联网车端中央计算单元（CCU）诊断服务模块

## 项目概述

本项目实现了车联网车端中央计算单元的诊断业务功能，包括：

- **UDS 诊断客户端**：实现统一诊断服务（UDS）协议通信
- **DID 注册表管理**：管理诊断标识符（DID）配置
- **诊断路由**：支持 DoCAN 和 DoIP 双传输协议
- **版本信息采集**：采集车辆 ECU 软件版本信息
- **FOTA 接口**：提供固件空中升级的诊断支持

## 目录结构

```
iov-vehicle-ccu-diag/
├── CMakeLists.txt          # CMake 构建配置
├── README.md               # 项目说明文档
├── LICENSE                 # MIT 许可证
├── config/                 # 配置目录（只安装 diag.default.yaml + schema + seed 模板）
│   ├── diag.default.yaml   # 统一正式配置模板（安装为 /etc/ccu/conf.d/diag.yaml）
│   ├── ecu-inventory.seed.json # ECU 清单 seed 结构模板（安装到 /usr/share/ccu-diag/）
│   ├── schema/             # diag.yaml 的 JSON Schema
│   └── dev/                # 本地开发夹具，永不安装
├── include/                # 头文件目录
│   ├── config_loader.h     # 配置加载器
│   ├── constants.h         # 常量定义
│   ├── data_models.h       # 数据模型
│   ├── diagnostic_router.h # 诊断路由器
│   ├── did_registry.h      # DID 注册表
│   ├── do_can_transport.h  # DoCAN 传输层
│   ├── doip_transport.h    # DoIP 传输层
│   ├── error_codes.h       # 错误码定义
│   ├── ecu_inventory_store.h    # 本地 ECU 清单事实存储（原子写/seed 导入）
│   ├── diag_payload_codec.h     # 0x0002 payload v1 / 0x0005 编解码
│   ├── fota_version_registrar.h # 0x0005 安装结果登记（幂等/处置）
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

- **CMake** >= 3.24
- **C++17** 编译器
- **yaml-cpp**：YAML 配置文件解析
- **nlohmann_json**：JSON 数据处理
- **Google Test**：单元测试框架（仅 `BUILD_TESTING=ON` 时查找）
- **CCUFramework**：已安装的 Framework SDK（`find_package(CCUFramework CONFIG REQUIRED)`）
- **vsomeip3 / Boost**：仅在 `USE_MOCK_SOMEIP=OFF` 的真实后端构建需要

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

# 开发/CI：显式启用 mock SOME/IP 与测试
cmake .. -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DUSE_MOCK_SOMEIP=ON \
  -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH="$HOME/.local"

# 编译
cmake --build . -j$(nproc)

# 运行测试
ctest --output-on-failure

# 生产 TARGET 构建由 CCU-BUILD 显式传入：
#   -DUSE_MOCK_SOMEIP=OFF -DBUILD_TESTING=OFF
```

## 使用方法

### 命令行选项

```bash
ccu_diag [选项]

选项：
  --diag-config <path>     统一 DIAG 配置 YAML（默认 /etc/ccu/conf.d/diag.yaml）
  --common-config <path>   公共配置 YAML（开发/测试覆盖，默认 /etc/ccu/common.yaml）
  --did-registry <path>    开发/测试覆盖：legacy DID 注册表 YAML
  --someip-config <path>   开发/测试覆盖：legacy SOME/IP 配置 YAML
  --sec-config <path>      开发/测试覆盖：legacy CCU-SEC SOME/IP 配置 YAML
  --can-interface <name>   CAN 接口名称
  --can-baudrate <rate>    CAN 波特率
  --doip-ip <ip>           DoIP 服务器 IP 地址
  --doip-port <port>       DoIP 服务器端口
  --someip-ip <ip>         SOME/IP 监听地址
  --someip-port <port>     SOME/IP 监听端口
  --version                打印应用版本
  --help                   显示帮助信息
```

### 示例

```bash
# 打印版本
./build/ccu_diag --version

# 使用开发夹具运行（统一配置 + 公共配置）
./build/ccu_diag \
  --diag-config config/dev/diag.yaml \
  --common-config config/dev/common.yaml

# 开发/测试覆盖单一域
./build/ccu_diag \
  --diag-config config/dev/diag.yaml \
  --common-config config/dev/common.yaml \
  --did-registry config/dev/legacy/did_registry.yaml
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

CCU-DIAG-DSN-CR-009 起，正式服务配置只有单一文件
`/etc/ccu/conf.d/diag.yaml`，顶层只有 `diag:` 根节点。公共
`common.log.*` 来自 BUILD 提供的 `/etc/ccu/common.yaml`，不得在
`diag.yaml` 内联顶层 `common:`。

```yaml
diag:
  log:        # 服务级日志覆盖（common.log.* 不在本文件）
  timing:     # UDS / 诊断时序
  retry:      # 重试次数
  transport:  # CAN / DoIP 非秘密参数（平台值由 BUILD overlay 提供）
  registry:   # 原 did_registry.yaml：registryVersion / baselineDids / ecus
  inventory:  # ECU 清单来源：mode(hardware|seed) / seed_path / state_path / 幂等环容量
  someip:     # 原 someip_config.yaml：provider 寻址与方法/事件
  sec_client:
    someip:   # 原 sec_config.yaml：CCU-SEC 对端配置
```

- `config/diag.default.yaml` 是仓库维护的安全默认模板；安装时重命名为
  `diag.yaml`，并由 BUILD 平台完整文件整文件覆盖。
- `config/schema/diag.schema.yaml` 是仓库维护的 Draft-07 schema；BUILD 与
  `ci/verify-config-schema.py` 使用同一版本校验默认/平台/最终文件。
- 旧四份配置保留在 `config/dev/legacy/` 仅作开发/测试覆盖入口，不进入
  `ccu-diag-runtime` 安装集。
- VIN 是设备实例状态，默认 `/var/lib/ccu/diag/vin.dat`，不进入发布包；
  部署与回退必须 preserve。

### ECU 清单来源（hardware / seed）

`diag.inventory.mode` 二选一、互斥，切换需重启：

- `hardware`（默认，生产目标态）：经 DoCAN/DoIP 用 UDS 0x22 逐 ECU 真实
  读取版本；单点失败只标记该 ECU 的 `status`/`errorCode`，`inventorySource=hardware`。
- `seed`（过渡兜底）：不发起对下诊断，清单事实来自部署 seed 声明表，
  `inventorySource=seed`，用于暂无真实 ECU 的联调/演示环境。

两类文件不要混淆：

| 文件 | 默认路径 | 性质 |
|------|----------|------|
| 清单记录 | `/var/lib/ccu/diag/ecu_inventory.json` | 设备实例状态（preserve），DIAG 原子写入，0x0005 登记后更新 |
| seed | `/usr/share/ccu-diag/ecu-inventory.seed.json` | 只读发布物，平台/BUILD 提供内容；仅在 `mode=seed` 且**无记录**时导入一次 |

启用 seed 的前置条件：

1. seed 中每个 `ecuId` 必须已登记在 `diag.registry.ecus`，否则启动
   fail-closed（`CCU-DIAG-1013`），0x0002 返回错误而非空清单。
1. seed 中每个 ECU 必须声明非空 `softwarePartNumber`（DID 0xF188）与
   `swVersion`（DID 0xF195），否则按 `CCU-DIAG-1013` 拒绝导入
   （CCU-DIAG-DSN-CR-012 D4）。
2. VIN 必须已预置（FOTA 侧把非空 `vin` 作为清单有效条件）。
3. 记录已存在时不会重新导入 seed；平台更换 seed 内容后如需重导，属运维操作
   （清理实例状态记录），不在运行时自动覆盖。
4. `state_path` 必须位于 `/var/lib/ccu/diag/` 下；记录损坏/校验不符/未知
   `schemaVersion` 返回 `CCU-DIAG-1012`，不自动重建、不返回空清单成功。

### ECU 件号与版本事实（CCU-DIAG-DSN-CR-012）

五项事实独立建模、互不推导（任何字段不得由其他字段补齐或冒充）：

| 字段 | 来源 DID | 含义 |
|------|----------|------|
| `partNumber` | 0xF187 | 总成/备件零件号 |
| `softwarePartNumber` | 0xF188 | ECU 软件零件号 |
| `swVersion` | 0xF195 | 软件版本 |
| `hwVersion` | 0xF193 | 硬件版本 |
| `serialNumber` | 0xF18C | ECU 实例序列号 |

- DidRegistry 的 canonical 字段名为 `softwarePartNumber`；旧名
  `ecuSoftwareNumber` 不再映射，也不保留第二套输出名称。
- 清单记录、seed 与 0x0002 payload 均携带 `softwarePartNumber`，并纳入记录
  checksum / seedDigest 完整性校验。
- 未知软件件号显式输出空串（`ecuList[].softwarePartNumber = ""`），
  绝不复用 `partNumber`；由 FOTA 侧据此拒绝上报。
- 开发阶段口径：`schemaVersion` 保持 `1`，`softwarePartNumber` 作为 v1 增量
  字段，不升 v2、不做 v1→v2 记录迁移。旧清单记录缺少该字段时读取为
  “未知”，不自动回填；如需取得经批准的软件件号，需清理实例状态记录后重导
  seed（仅开发/测试阶段操作）。

## 测试

### 运行测试

```bash
# 运行所有测试
cd build && ctest --output-on-failure

# 或者直接运行测试可执行文件
./build/CcuDiagTests

# 运行特定测试
./build/CcuDiagTests --gtest_filter="*ConfigLoader*"
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
- VIN 存储与管理
- 诊断服务器（UDS 0x27/0x2E）
- VIN 配置集成测试

## 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件

## 贡献指南

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 创建 Pull Request

## SOME/IP 服务

CCU-DIAG 现在作为独立进程运行，通过 SOME/IP 提供服务间通信能力。

### 服务配置

SOME/IP provider 配置位于统一 `diag.yaml` 的 `diag.someip` 子树；CCU-SEC
对端配置位于 `diag.sec_client.someip`。二者共用同一个进程 application
`ccu-diag`。

### 命令行参数

```
./ccu_diag [options]
Options:
  --diag-config <path>     Path to unified DIAG config YAML
  --common-config <path>   Path to common config YAML (dev/test override)
  --did-registry <path>    Dev/test override: legacy DID registry YAML
  --someip-config <path>   Dev/test override: legacy SOME/IP config YAML
  --sec-config <path>      Dev/test override: legacy CCU-SEC SOME/IP config YAML
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
| readVin | 0x0001 | 读取 VIN |
| collectVehicleInventory | 0x0002 | 触发一轮采集，返回 VehicleSoftwareSnapshot |
| getEcuVersion | 0x0003 | 按单 ECU 读取版本 |
| getRegistryVersion | 0x0004 | 返回当前 DidRegistry 版本 |
| recordInstalledVersions | 0x0005 | 登记 FOTA 安装结果，幂等更新本地清单版本事实（建议 ID，待 VEH-SOMEIP Registry 复核） |

## BUILD 接入（服务侧契约）

CCU-DIAG-DSN-CR-009 定义的服务侧交付契约：

- 安装 component：`ccu-diag-runtime`（仅 `ccu_diag`、`ccu-diag.service`、
  `diag.yaml`、`diag.schema.yaml`、`ecu-inventory.seed.json`）。
- 生产 configure 输入：`BUILD_TESTING=OFF`、`USE_MOCK_SOMEIP=OFF`。
- 正式配置：`/etc/ccu/common.yaml`（BUILD 提供）+ `/etc/ccu/conf.d/diag.yaml`
  （服务默认模板 + 平台整文件覆盖）。
- 实例状态：`/var/lib/ccu/diag/**`（preserve，不进入普通发布包）。
- 配置校验：`config/schema/diag.schema.yaml` + `ci/verify-config-schema.py`。
- smoke 入口：`tests/smoke/smoke-diag.sh`。
- `services.yaml` / `release-sets.yaml` / dependency lock 的正式登记由对应
  CCU-BUILD CR 授权；本仓库不直接修改 BUILD 清单。

## 联系方式

- 项目维护者：hwyz_leo
- 项目链接：https://github.com/hwyzleo/iov-vehicle-ccu-diag