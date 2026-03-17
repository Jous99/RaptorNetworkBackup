// Copyright 2020 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "core/hle/kernel/k_event.h"
#include "core/hle/result.h"

namespace Core {
class System;
}

namespace Service::Account {

class AsyncOperation {
public:
    explicit AsyncOperation(Core::System& system, std::function<bool()> work);
    ~AsyncOperation();

    void Cancel();

    [[nodiscard]] std::shared_ptr<Kernel::KEvent> GetSystemEvent();

    [[nodiscard]] bool HasDone();

    [[nodiscard]] ResultCode GetResult();

private:
    void WorkerThread(Core::System& system, const std::function<bool()>& work);

    ResultCode output_result = RESULT_SUCCESS;
    bool has_done = false;
    bool is_cancelled = false;

    std::shared_ptr<Kernel::KEvent> event;

    std::thread thread;
    std::mutex mutex;
};

} // namespace Service::Account
