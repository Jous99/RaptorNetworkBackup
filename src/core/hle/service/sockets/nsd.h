// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Sockets {

class NSD final : public ServiceFramework<NSD> {
public:
    explicit NSD(Core::System& system, const char* name);
    ~NSD() override;

private:
    void ResolveEx(Kernel::HLERequestContext& ctx);
};

} // namespace Service::Sockets
