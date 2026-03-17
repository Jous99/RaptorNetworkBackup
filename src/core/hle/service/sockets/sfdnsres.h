// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/kernel/hle_ipc.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sockets/sockets.h"
#include "core/network/network.h"

namespace Core {
class System;
}

namespace Core {
class System;
}

namespace Service::Sockets {

class SFDNSRES final : public ServiceFramework<SFDNSRES> {
public:
    explicit SFDNSRES(Core::System& system_);
    ~SFDNSRES() override;

private:
    struct GetHostByNameWork {
        void Execute(SFDNSRES* sfdnsres);
        void Response(Kernel::HLERequestContext& ctx);

        u8 use_nsd_resolve;
        u32 cancel_handle;
        u64 process_id;

        std::string hostname;

        s32 result_errno = 0;
        s32 result_h_errno = 0;
        std::vector<u8> result_hostent;
    };

    struct GetHostByNameWithOptionsWork : public GetHostByNameWork {
        void Response(Kernel::HLERequestContext& ctx);
    };

    struct GetAddrInfoWork {
        void Execute(SFDNSRES* sfdnsres);
        void Response(Kernel::HLERequestContext& ctx);

        u8 use_nsd_resolve;
        u32 cancel_handle;
        u64 process_id;

        std::string node;
        std::vector<char> service;
        std::vector<Network::AddrInfo> hints;

        s32 result_value = 0;
        s32 result_errno = 0;
        std::vector<u8> serialized_addr_infos;
    };

    struct GetAddrInfoWithOptionsWork : public GetAddrInfoWork {
        void Response(Kernel::HLERequestContext& ctx);
    };

    static GetHostByNameWork MakeGetHostByNameWork(Kernel::HLERequestContext& ctx);
    static GetAddrInfoWork MakeGetAddrInfoWork(Kernel::HLERequestContext& ctx);

    void GetHostByNameRequest(Kernel::HLERequestContext& ctx);
    void GetAddrInfoRequest(Kernel::HLERequestContext& ctx);
    void GetCancelHandleRequest(Kernel::HLERequestContext& ctx);
    void GetHostByNameRequestWithOptions(Kernel::HLERequestContext& ctx);
    void GetAddrInfoRequestWithOptions(Kernel::HLERequestContext& ctx);

    template <typename Work>
    void ExecuteWork(Kernel::HLERequestContext& ctx, Work work);

    u32 current_cancel_handle = 0;
};

} // namespace Service::Sockets
