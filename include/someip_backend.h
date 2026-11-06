#pragma once

// =============================================================================
// include/someip_backend.h
// 进程级 SOME/IP 后端单例（CCU-DIAG-DSN-CR-002 §1.1 / CCU-DIAG-DSN-CR-007）
// =============================================================================
// vsomeip 的 application 身份由平台配置 /etc/ccu/someip/vsomeip.json 的
// applications 列表决定（ccu-fota=0x1310 / ccu-diag=0x1110 / ccu-sec=0x1210），
// 且一个进程只允许一个 application 加入某个 routing 域。
//
// 修复前：service 用 "CcuDiagService_<decimal service_id>"、client proxy 用
// "CcuDiagClient_<decimal service_id>"，两个名字都没有在 applications 列表中
// 登记，于是各自落到默认 routing（vsomeip）而不在 tbox-someip 域内，结果
// CCU-FOTA 侧永远看不到 0x1110 的 offer（DIAG service unavailable）。
//
// 修复后：本进程所有 SOME/IP 使用（service offer 与 client proxy 的
// request/response）共享同一个已登记 application（缺省 "ccu-diag"）。
// =============================================================================

#ifdef USE_MOCK_SOMEIP
#include "mock_someip.h"
namespace someip = mock_someip;
#else
#include <vsomeip/vsomeip.hpp>
namespace someip = vsomeip;
#endif

#include <memory>
#include <string>

namespace ccu_diag {

// 取（必要时创建并 init）本进程唯一的 vsomeip application。
//   * name 为空 -> constants::DEFAULT_SOMEIP_APPLICATION_NAME；
//   * 重复调用返回同一实例（不再重复 init）；
//   * 请求名与已创建名不一致时复用已创建实例并告警（application 身份一经
//     建立不可在同一进程内更换）。
std::shared_ptr<someip::Application> acquireProcessApplication(const std::string& name);

} // namespace ccu_diag
