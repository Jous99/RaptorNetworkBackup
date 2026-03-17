// Copyright 2020 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <string>
#include <string_view>

#include <fmt/format.h>

#include <nlohmann/json.hpp>

#include <httplib.h>

#include "common/common_types.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/hle/kernel/k_writable_event.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/acc/token_granter.h"
#include "core/online_initiator.h"

namespace Service::Account {

constexpr ResultCode RESULT_CANCELLED{ErrorModule::Account, 0};
constexpr ResultCode RESULT_NETWORK_ERROR{ErrorModule::Account, 3000};

AsyncOperation::AsyncOperation(Core::System& system, std::function<bool()> work) {
    event = Kernel::KEvent::Create(system.Kernel(), "IAsyncContext:AsyncOperation");
    event->Initialize();
    thread = std::thread([this, &system, work = std::move(work)] { WorkerThread(system, work); });
}

AsyncOperation::~AsyncOperation() {
    thread.join();
}

void AsyncOperation::Cancel() {
    std::scoped_lock lock{mutex};
    if (has_done) {
        LOG_WARNING(Service_ACC, "Cancelling a finished operation");
    }
    output_result = RESULT_CANCELLED;
}

std::shared_ptr<Kernel::KEvent> AsyncOperation::GetSystemEvent() {
    std::scoped_lock lock{mutex};
    return event;
}

bool AsyncOperation::HasDone() {
    std::scoped_lock lock{mutex};
    return has_done;
}

ResultCode AsyncOperation::GetResult() {
    std::scoped_lock lock{mutex};
    if (!has_done) {
        LOG_ERROR(Service_ACC, "Asynchronous result read before it was written");
    }
    return output_result;
}

void AsyncOperation::WorkerThread(Core::System& system, const std::function<bool()>& work) {
    Common::SetCurrentThreadName("TokenGranter");
    system.Kernel().RegisterHostThread();

    const auto result = work();

    std::scoped_lock lock{mutex};
    if (output_result == RESULT_CANCELLED) {
        return;
    }
    output_result = result ? RESULT_SUCCESS : RESULT_NETWORK_ERROR;

    has_done = true;
    event->GetWritableEvent()->Signal();

    LOG_INFO(Service_ACC, "Asynchronous operation has completed");
}

} // namespace Service::Account
