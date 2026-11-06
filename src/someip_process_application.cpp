// =============================================================================
// src/someip_process_application.cpp
// 进程级 vsomeip application 单例实现（见 include/someip_backend.h）
// =============================================================================

#include "someip_backend.h"

#include "constants.h"
#include "diag_log_adapter.h"

#include <mutex>

namespace ccu_diag {

namespace {
std::mutex g_mutex;
// 进程内唯一的 application 实例：用强引用持有，避免第一个使用者销毁后
// 其余使用者拿到悬空实例（进程退出时随静态析构释放）。
std::shared_ptr<someip::Application> g_application;
std::string g_application_name;
} // namespace

std::shared_ptr<someip::Application> acquireProcessApplication(const std::string& name) {
    const std::string wanted =
        name.empty() ? constants::DEFAULT_SOMEIP_APPLICATION_NAME : name;

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_application) {
        if (!g_application_name.empty() && g_application_name != wanted) {
            // application 身份（name -> id）由平台配置固定，同进程内不能更换；
            // 复用已建立实例，避免出现第二个未登记的 application。
            DiagLogAdapter::transport().warn(
                "diag.someip.application_name_mismatch",
                "Process vsomeip application already established with another name, reusing it",
                {dlog::f_str("established", g_application_name),
                 dlog::f_str("requested", wanted)});
        }
        return g_application;
    }

    auto runtime = someip::Runtime::get();
    if (!runtime) {
        DiagLogAdapter::transport().error(
            "diag.someip.runtime_unavailable",
            "vsomeip runtime unavailable, application not created");
        return nullptr;
    }

    auto application = runtime->create_application(wanted);
    if (!application) {
        DiagLogAdapter::transport().error(
            "diag.someip.application_create_failed",
            "Failed to create vsomeip application",
            {dlog::f_str("application", wanted)});
        return nullptr;
    }

    application->init();

    g_application = application;
    g_application_name = wanted;

    DiagLogAdapter::transport().info(
        "diag.someip.application_established",
        "Process vsomeip application established (shared by service and client proxy)",
        {dlog::f_str("application", wanted)});

    return application;
}

} // namespace ccu_diag
