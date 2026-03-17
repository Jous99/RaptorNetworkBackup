// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <mutex>

#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/result.h"
#include "core/hle/service/ldn/backend/backend.h"
#include "core/hle/service/ldn/ldn.h"
#include "core/hle/service/sm/sm.h"
#include "core/network/network.h"
#include "core/network/sockets.h"

namespace Service::LDN {

static constexpr ResultCode RESULT_LDN_ERROR{ErrorModule::LDN, 32};

class IMonitorService final : public ServiceFramework<IMonitorService> {
public:
    explicit IMonitorService(Core::System& system_) : ServiceFramework{system_, "IMonitorService"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetStateForMonitor"},
            {1, nullptr, "GetNetworkInfoForMonitor"},
            {2, nullptr, "GetIpv4AddressForMonitor"},
            {3, nullptr, "GetDisconnectReasonForMonitor"},
            {4, nullptr, "GetSecurityParameterForMonitor"},
            {5, nullptr, "GetNetworkConfigForMonitor"},
            {100, nullptr, "InitializeMonitor"},
            {101, nullptr, "FinalizeMonitor"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class LDNM final : public ServiceFramework<LDNM> {
public:
    explicit LDNM(Core::System& system_) : ServiceFramework{system_, "ldn:m"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &LDNM::CreateMonitorService, "CreateMonitorService"}
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void CreateMonitorService(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LDN, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<IMonitorService>(system);
    }
};

class ISystemLocalCommunicationService final
    : public ServiceFramework<ISystemLocalCommunicationService> {
public:
    explicit ISystemLocalCommunicationService(Core::System& system_)
        : ServiceFramework{system_, "ISystemLocalCommunicationService"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetState"},
            {1, nullptr, "GetNetworkInfo"},
            {2, nullptr, "GetIpv4Address"},
            {3, nullptr, "GetDisconnectReason"},
            {4, nullptr, "GetSecurityParameter"},
            {5, nullptr, "GetNetworkConfig"},
            {100, nullptr, "AttachStateChangeEvent"},
            {101, nullptr, "GetNetworkInfoLatestUpdate"},
            {102, nullptr, "Scan"},
            {103, nullptr, "ScanPrivate"},
            {104, nullptr, "SetWirelessControllerRestriction"},
            {200, nullptr, "OpenAccessPoint"},
            {201, nullptr, "CloseAccessPoint"},
            {202, nullptr, "CreateNetwork"},
            {203, nullptr, "CreateNetworkPrivate"},
            {204, nullptr, "DestroyNetwork"},
            {205, nullptr, "Reject"},
            {206, nullptr, "SetAdvertiseData"},
            {207, nullptr, "SetStationAcceptPolicy"},
            {208, nullptr, "AddAcceptFilterEntry"},
            {209, nullptr, "ClearAcceptFilter"},
            {300, nullptr, "OpenStation"},
            {301, nullptr, "CloseStation"},
            {302, nullptr, "Connect"},
            {303, nullptr, "ConnectPrivate"},
            {304, nullptr, "Disconnect"},
            {400, nullptr, "InitializeSystem"},
            {401, nullptr, "FinalizeSystem"},
            {402, nullptr, "SetOperationMode"},
            {403, nullptr, "InitializeSystem2"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IUserLocalCommunicationService final
    : public ServiceFramework<IUserLocalCommunicationService> {
public:
    explicit IUserLocalCommunicationService(Core::System& system_)
        : ServiceFramework{system_, "IUserLocalCommunicationService"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IUserLocalCommunicationService::GetState, "GetState"},
            {1, &IUserLocalCommunicationService::GetNetworkInfo, "GetNetworkInfo"},
            {2, &IUserLocalCommunicationService::GetIpv4Address, "GetIpv4Address"},
            {3, &IUserLocalCommunicationService::GetDisconnectReason, "GetDisconnectReason"},
            {4, &IUserLocalCommunicationService::GetSecurityParameter, "GetSecurityParameter"},
            {5, nullptr, "GetNetworkConfig"},
            {100, nullptr, "AttachStateChangeEvent"},
            {101, nullptr, "GetNetworkInfoLatestUpdate"},
            {102, &IUserLocalCommunicationService::Scan, "Scan"},
            {103, nullptr, "ScanPrivate"},
            {104, nullptr, "SetWirelessControllerRestriction"},
            {200, &IUserLocalCommunicationService::OpenAccessPoint, "OpenAccessPoint"},
            {201, &IUserLocalCommunicationService::CloseAccessPoint, "CloseAccessPoint"},
            {202, &IUserLocalCommunicationService::CreateNetwork, "CreateNetwork"},
            {203, nullptr, "CreateNetworkPrivate"},
            {204, nullptr, "DestroyNetwork"},
            {205, nullptr, "Reject"},
            {206, &IUserLocalCommunicationService::SetAdvertiseData, "SetAdvertiseData"},
            {207, nullptr, "SetStationAcceptPolicy"},
            {208, nullptr, "AddAcceptFilterEntry"},
            {209, nullptr, "ClearAcceptFilter"},
            {300, &IUserLocalCommunicationService::OpenStation, "OpenStation"},
            {301, &IUserLocalCommunicationService::CloseStation, "CloseStation"},
            {302, &IUserLocalCommunicationService::Connect, "Connect"},
            {303, nullptr, "ConnectPrivate"},
            {304, &IUserLocalCommunicationService::Disconnect, "Disconnect"},
            {400, &IUserLocalCommunicationService::Initialize, "Initialize"},
            {401, &IUserLocalCommunicationService::Finalize, "Finalize"},
            {402, &IUserLocalCommunicationService::Initialize2, "Initialize2"}, // 7.0.0+
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void GetState(Kernel::HLERequestContext& ctx);
    void GetNetworkInfo(Kernel::HLERequestContext& ctx);
    void GetIpv4Address(Kernel::HLERequestContext& ctx);
    void GetDisconnectReason(Kernel::HLERequestContext& ctx);
    void GetSecurityParameter(Kernel::HLERequestContext& ctx);
    void Scan(Kernel::HLERequestContext& ctx);
    void OpenAccessPoint(Kernel::HLERequestContext& ctx);
    void CloseAccessPoint(Kernel::HLERequestContext& ctx);
    void CreateNetwork(Kernel::HLERequestContext& ctx);
    void SetAdvertiseData(Kernel::HLERequestContext& ctx);
    void OpenStation(Kernel::HLERequestContext& ctx);
    void CloseStation(Kernel::HLERequestContext& ctx);
    void Connect(Kernel::HLERequestContext& ctx);
    void Disconnect(Kernel::HLERequestContext& ctx);
    void Initialize(Kernel::HLERequestContext& ctx);
    void Finalize(Kernel::HLERequestContext& ctx);
    void Initialize2(Kernel::HLERequestContext& ctx);

    LanImpl lan;
};

void IUserLocalCommunicationService::GetState(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.PushEnum(lan.GetState());
}

void IUserLocalCommunicationService::GetNetworkInfo(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    const std::optional<NetworkInfo> network_info = lan.GetNetworkInfo();
    if (network_info) {
        ctx.WriteBuffer(&*network_info, sizeof(*network_info));
    }

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(network_info ? RESULT_SUCCESS : RESULT_LDN_ERROR);
}

void IUserLocalCommunicationService::GetIpv4Address(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    const std::optional<std::pair<IPv4Address, Netmask>> result = lan.GetIpv4Address();
    ASSERT(result);
    const auto [address, netmask] = *result;

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);
    rb.PushRaw(address);
    rb.PushRaw(netmask);
}

void IUserLocalCommunicationService::GetDisconnectReason(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    const std::optional<DisconnectReason> result = lan.GetDisconnectReason();
    ASSERT(result);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u16>(static_cast<u16>(*result));
}

void IUserLocalCommunicationService::GetSecurityParameter(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    const std::optional<SecurityParameter> result = lan.GetSecurityParameter();
    ASSERT(result);

    IPC::ResponseBuilder rb{ctx, 10};
    rb.Push(RESULT_SUCCESS);
    rb.PushRaw(*result);
}

void IUserLocalCommunicationService::Scan(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    [[maybe_unused]] const s16 channel = rp.Pop<s16>();
    [[maybe_unused]] const u16 padding = rp.Pop<u16>();
    [[maybe_unused]] const auto scan_filter = rp.PopRaw<ScanFilter>();

    LOG_WARNING(Service_LDN, "(STUBBED) called");

    const std::optional<std::vector<NetworkInfo>> networks = lan.Scan(channel, scan_filter);
    ASSERT(networks);
    ctx.WriteBuffer(*networks);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(static_cast<u32>(networks->size()));
}

void IUserLocalCommunicationService::OpenAccessPoint(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    const bool success = lan.OpenAccessPoint();
    ASSERT(success);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IUserLocalCommunicationService::CloseAccessPoint(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    const bool success = lan.CloseAccessPoint();
    ASSERT(success);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IUserLocalCommunicationService::CreateNetwork(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto security_config = rp.PopRaw<SecurityConfig>();
    const auto user_config = rp.PopRaw<UserConfig>();
    rp.Skip(1, false);
    const auto network_config = rp.PopRaw<NetworkConfig>();

    LOG_WARNING(Service_LDN, "(STUBBED) called");

    const bool success = lan.CreateNetwork(security_config, user_config, network_config);
    ASSERT(success);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IUserLocalCommunicationService::SetAdvertiseData(Kernel::HLERequestContext& ctx) {
    const std::vector<u8> advertise_data = ctx.ReadBuffer();

    LOG_WARNING(Service_LDN, "(STUBBED) called");

    const bool success = lan.SetAdvertiseData(advertise_data);
    ASSERT(success);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IUserLocalCommunicationService::OpenStation(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    const bool success = lan.OpenStation();
    ASSERT(success);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IUserLocalCommunicationService::CloseStation(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    const bool success = lan.CloseStation();
    ASSERT(success);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IUserLocalCommunicationService::Connect(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    [[maybe_unused]] const auto network_info = rp.PopRaw<NetworkInfo>();
    [[maybe_unused]] const auto security_config = rp.PopRaw<SecurityConfig>();
    [[maybe_unused]] const auto user_config = rp.PopRaw<UserConfig>();
    [[maybe_unused]] const u32 local_communication_version = rp.Pop<u32>();
    [[maybe_unused]] const u32 connect_option = rp.Pop<u32>();

    LOG_WARNING(Service_LDN, "(STUBBED) called");

    const bool success = lan.Connect();
    ASSERT(success);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IUserLocalCommunicationService::Disconnect(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    const bool success = lan.Disconnect();
    ASSERT(success);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IUserLocalCommunicationService::Initialize(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    const bool success = lan.Initialize();
    ASSERT(success);

    // TODO: Only return success if a setting is enabled
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IUserLocalCommunicationService::Finalize(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    const bool success = lan.Finalize();
    ASSERT(success);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IUserLocalCommunicationService::Initialize2(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    const bool success = lan.Initialize();
    ASSERT(success);

    // TODO: Only return success if a setting is enabled
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

class LDNS final : public ServiceFramework<LDNS> {
public:
    explicit LDNS(Core::System& system_) : ServiceFramework{system_, "ldn:s"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &LDNS::CreateSystemLocalCommunicationService, "CreateSystemLocalCommunicationService"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void CreateSystemLocalCommunicationService(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LDN, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<ISystemLocalCommunicationService>(system);
    }
};

class LDNU final : public ServiceFramework<LDNU> {
public:
    explicit LDNU(Core::System& system_) : ServiceFramework{system_, "ldn:u"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &LDNU::CreateUserLocalCommunicationService, "CreateUserLocalCommunicationService"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void CreateUserLocalCommunicationService(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LDN, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<IUserLocalCommunicationService>(system);
    }
};

void InstallInterfaces(SM::ServiceManager& sm, Core::System& system) {
    std::make_shared<LDNM>(system)->InstallAsService(sm);
    std::make_shared<LDNS>(system)->InstallAsService(sm);
    std::make_shared<LDNU>(system)->InstallAsService(sm);
}

} // namespace Service::LDN
