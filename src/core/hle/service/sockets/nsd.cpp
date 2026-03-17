// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <string>

#include <fmt/format.h>

#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/sockets/nsd.h"
#include "core/online_initiator.h"

namespace Service::Sockets {

void NSD::ResolveEx(Kernel::HLERequestContext& ctx) {
    const std::string dns = Common::StringFromBuffer(ctx.ReadBuffer());

    LOG_DEBUG(Service, "called. dns='{}'", dns);

    const Core::OnlineInitiator& online_initiator = system.OnlineInitiator();
    const std::string response = online_initiator.ResolveUrl(dns, true);

    // Include the zero terminator
    ctx.WriteBuffer(response.c_str(), response.size() + 1);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(0);
}

NSD::NSD(Core::System& system_, const char* name) : ServiceFramework{system_, name} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {5, nullptr, "GetSettingUrl"},
        {10, nullptr, "GetSettingName"},
        {11, nullptr, "GetEnvironmentIdentifier"},
        {12, nullptr, "GetDeviceId"},
        {13, nullptr, "DeleteSettings"},
        {14, nullptr, "ImportSettings"},
        {15, nullptr, "SetChangeEnvironmentIdentifierDisabled"},
        {20, nullptr, "Resolve"},
        {21, &NSD::ResolveEx, "ResolveEx"},
        {30, nullptr, "GetNasServiceSetting"},
        {31, nullptr, "GetNasServiceSettingEx"},
        {40, nullptr, "GetNasRequestFqdn"},
        {41, nullptr, "GetNasRequestFqdnEx"},
        {42, nullptr, "GetNasApiFqdn"},
        {43, nullptr, "GetNasApiFqdnEx"},
        {50, nullptr, "GetCurrentSetting"},
        {51, nullptr, "WriteTestParameter"},
        {52, nullptr, "ReadTestParameter"},
        {60, nullptr, "ReadSaveDataFromFsForTest"},
        {61, nullptr, "WriteSaveDataToFsForTest"},
        {62, nullptr, "DeleteSaveDataOfFsForTest"},
        {63, nullptr, "IsChangeEnvironmentIdentifierDisabled"},
        {64, nullptr, "SetWithoutDomainExchangeFqdns"},
        {100, nullptr, "GetApplicationServerEnvironmentType"},
        {101, nullptr, "SetApplicationServerEnvironmentType"},
        {102, nullptr, "DeleteApplicationServerEnvironmentType"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

NSD::~NSD() = default;

} // namespace Service::Sockets
