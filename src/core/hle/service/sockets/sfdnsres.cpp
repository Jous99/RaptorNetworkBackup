// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <optional>
#include <type_traits>
#include <vector>

#include "common/common_types.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/sockets/sfdnsres.h"
#include "core/hle/service/sockets/sockets_translate.h"
#include "core/network/network.h"
#include "core/online_initiator.h"

namespace Service::Sockets {

namespace {

template <typename T, bool big_endian = !std::is_class_v<T>>
void Insert(std::vector<u8>& vector, T value) {
    std::array<char, sizeof(T)> array;
    std::memcpy(array.data(), &value, sizeof(value));

    // We have to insert data in big endian instead of little endian
    if constexpr (big_endian) {
        std::reverse(array.begin(), array.end());
    }

    vector.resize(vector.size() + sizeof(value));
    std::memcpy(vector.data() + vector.size() - sizeof(value), array.data(), array.size());
}

void Insert(std::vector<u8>& vector, const void* data, size_t size) {
    vector.resize(vector.size() + size);
    std::memcpy(vector.data() + vector.size() - size, data, size);
}

template <typename T, bool big_endian = !std::is_class_v<T>>
T Pop(u8*& pos) {
    // If data is in big endian, we have to reverse it
    std::array<char, sizeof(T)> array;
    std::memcpy(array.data(), pos, array.size());
    if constexpr (big_endian) {
        std::reverse(array.begin(), array.end());
    }
    pos += array.size();

    T result;
    std::memcpy(&result, array.data(), array.size());
    return result;
}

[[maybe_unused]] std::vector<u8> Pop(u8*& pos, size_t len) {
    std::vector<u8> result(len);
    std::memcpy(result.data(), pos, len);
    pos += len;
    return result;
}

std::vector<u8> SerializeHostEnt(const Network::HostEnt& hostent) {
    std::vector<u8> result;

    Insert(result, hostent.name.data(), hostent.name.size());
    Insert<u8>(result, 0);

    Insert(result, static_cast<u32>(hostent.aliases.size()));
    for (const std::string& alias : hostent.aliases) {
        Insert(result, alias.data(), alias.size());
        Insert<u8>(result, 0);
    }

    UNIMPLEMENTED_IF(hostent.addr_type != Network::Domain::INET);
    Insert<u16>(result, 2); // addrtype=AF_INET
    Insert<u16>(result, 4); // addrlen=4

    Insert(result, static_cast<u32>(hostent.addr_list.size()));
    for (Network::IPv4Address addr : hostent.addr_list) {
        std::reverse(addr.begin(), addr.end());
        Insert(result, addr.data(), addr.size());
    }

    return result;
}

std::optional<Network::AddrInfo> DeserializeAddrInfo(u8*& pos) {
    const u32 magic = Pop<u32>(pos);
    if (magic == 0) {
        return std::nullopt;
    }
    ASSERT(magic == 0xbeefcafe);

    [[maybe_unused]] const u32 flags = Pop<u32>(pos);
    ASSERT(flags == 0);

    const auto family = Pop<Domain>(pos);
    const auto socktype = Pop<Type>(pos);
    const auto protocol = Pop<Protocol>(pos);

    const u32 addrlen = Pop<u32>(pos);
    SockAddrIn addr{};
    if (addrlen == sizeof(addr)) {
        addr = Pop<SockAddrIn>(pos);
    } else {
        ASSERT(addrlen == 0);
        pos += sizeof(u32); // Skip dummy bytes
    }

    const auto canonname = reinterpret_cast<const char*>(pos);
    const size_t len = std::strlen(canonname);
    pos += len + 1;

    return Network::AddrInfo{
        .family = Translate(family),
        .socket_type = Translate(socktype),
        .protocol = Translate(socktype, protocol),
        .addr = Translate(addr),
        .canonname = std::string(canonname, len),
    };
}

std::vector<Network::AddrInfo> DeserializeAddrInfos(std::vector<u8> input) {
    std::vector<Network::AddrInfo> result;
    u8* pos = input.data();
    while (std::optional<Network::AddrInfo> info = DeserializeAddrInfo(pos)) {
        result.push_back(std::move(*info));
    }
    return result;
}

void SerializeAddrInfo(const Network::AddrInfo& input, std::vector<u8>& result) {
    ASSERT(input.flags == 0);
    ASSERT(input.family == Network::Domain::INET);
    ASSERT(input.socket_type == Network::Type::UNSPECIFIED);

    Insert<u32>(result, 0xbeefcafe);
    Insert<u32>(result, 0); // flags
    Insert<Domain>(result, Domain::INET);
    Insert<Type>(result, Type::UNSPECIFIED);
    Insert<Protocol>(result, Translate(input.protocol));
    Insert<u32>(result, sizeof(SockAddrIn)); // addrlen

    // Apparently Nintendo reverses everything here
    SockAddrIn addr = Translate(input.addr);
    std::reverse(addr.ip.begin(), addr.ip.end());
    addr.portno = static_cast<u16>(addr.portno >> 8 | addr.portno << 8);
    Insert<SockAddrIn>(result, addr);

    if (input.canonname.empty()) {
        Insert<u8>(result, 0);
    } else {
        Insert(result, input.canonname.data(), input.canonname.size() + 1);
    }
}

std::vector<u8> SerializeAddrInfos(const std::vector<Network::AddrInfo>& input) {
    std::vector<u8> result;
    for (const Network::AddrInfo& addr_info : input) {
        SerializeAddrInfo(addr_info, result);
    }
    Insert<u32>(result, 0);
    return result;
}

} // Anonymous namespace

void SFDNSRES::GetHostByNameWork::Execute(SFDNSRES*) {
    const auto [hostent, err] = Network::GetHostByName(hostname.c_str());
    UNIMPLEMENTED_IF(err != Network::Errno::SUCCESS);

    result_errno = 0;
    result_h_errno = 0;
    result_hostent = SerializeHostEnt(hostent);
}

void SFDNSRES::GetHostByNameWork::Response(Kernel::HLERequestContext& ctx) {
    if (!result_hostent.empty()) {
        ctx.WriteBuffer(result_hostent);
    }

    IPC::ResponseBuilder rb{ctx, 5};
    rb.Push(RESULT_SUCCESS);
    rb.Push(result_h_errno);
    rb.Push(result_errno);
    rb.Push(static_cast<u32>(result_hostent.size()));
}

void SFDNSRES::GetHostByNameWithOptionsWork::Response(Kernel::HLERequestContext& ctx) {
    if (!result_hostent.empty()) {
        ctx.WriteBuffer(result_hostent);
    }

    IPC::ResponseBuilder rb{ctx, 5};
    rb.Push(RESULT_SUCCESS);
    rb.Push(static_cast<u32>(result_hostent.size()));
    rb.Push(result_h_errno);
    rb.Push(result_errno);
}

void SFDNSRES::GetAddrInfoWork::Execute(SFDNSRES*) {
    const auto [addrs, err] = Network::GetAddressInfo(
        node.empty() ? nullptr : node.data(), service.empty() ? nullptr : service.data(), hints);
    ASSERT(err == Network::Errno::SUCCESS);

    result_value = 0;
    result_errno = 0;
    serialized_addr_infos = SerializeAddrInfos(addrs);
}

void SFDNSRES::GetAddrInfoWork::Response(Kernel::HLERequestContext& ctx) {
    if (!serialized_addr_infos.empty()) {
        ctx.WriteBuffer(serialized_addr_infos);
    }

    IPC::ResponseBuilder rb{ctx, 5};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(result_errno);
    rb.Push<u32>(result_value);
    rb.Push<u32>(static_cast<u32>(serialized_addr_infos.size()));
}

void SFDNSRES::GetAddrInfoWithOptionsWork::Response(Kernel::HLERequestContext& ctx) {
    if (!serialized_addr_infos.empty()) {
        ctx.WriteBuffer(serialized_addr_infos);
    }

    IPC::ResponseBuilder rb{ctx, 5};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(static_cast<u32>(serialized_addr_infos.size()));
    rb.Push<u32>(result_errno);
    rb.Push<u32>(result_value);
}

SFDNSRES::GetHostByNameWork SFDNSRES::MakeGetHostByNameWork(Kernel::HLERequestContext& ctx) {
    struct Parameters {
        u8 use_nsd_resolve;
        u32 cancel_handle;
        u64 process_id;
    };

    IPC::RequestParser rp{ctx};
    const auto parameters = rp.PopRaw<Parameters>();

    return GetHostByNameWork{
        .use_nsd_resolve = parameters.use_nsd_resolve,
        .cancel_handle = parameters.cancel_handle,
        .process_id = parameters.process_id,
        .hostname = Common::StringFromBuffer(ctx.ReadBuffer(0)),
    };
}

SFDNSRES::GetAddrInfoWork SFDNSRES::MakeGetAddrInfoWork(Kernel::HLERequestContext& ctx) {
    struct Parameters {
        u8 use_nsd_resolve;
        u32 cancel_handle;
        u64 process_id;
    };

    IPC::RequestParser rp{ctx};
    const auto parameters = rp.PopRaw<Parameters>();

    std::string node;
    std::vector<char> service;
    std::vector<Network::AddrInfo> hints;

    const auto& input_buffers = ctx.BufferDescriptorA();
    if (input_buffers[0].Size() > 0) {
        auto input = ctx.ReadBuffer(0);
        node.insert(node.begin(), input.begin(), input.end());
    }
    if (input_buffers[1].Size() > 0) {
        auto input = ctx.ReadBuffer(1);
        service.insert(service.begin(), input.begin(), input.end());
    }
    if (input_buffers[2].Size() > 0) {
        hints = DeserializeAddrInfos(ctx.ReadBuffer(2));
    }

    return GetAddrInfoWork{
        .use_nsd_resolve = parameters.use_nsd_resolve,
        .cancel_handle = parameters.cancel_handle,
        .process_id = parameters.process_id,
        .node = std::move(node),
        .service = std::move(service),
        .hints = std::move(hints),
    };
}

void SFDNSRES::GetHostByNameRequest(Kernel::HLERequestContext& ctx) {
    GetHostByNameWork work = MakeGetHostByNameWork(ctx);

    LOG_DEBUG(Service,
              "called. use_nsd_resolve={}, cancel_handle=0x{:x}, process_id=0x{:016X} "
              "hostname='{}'",
              work.use_nsd_resolve, work.cancel_handle, work.process_id, work.hostname);

    work.hostname = system.OnlineInitiator().ResolveUrl(work.hostname, work.use_nsd_resolve != 0);

    ExecuteWork(ctx, std::move(work));
}

void SFDNSRES::GetAddrInfoRequest(Kernel::HLERequestContext& ctx) {
    GetAddrInfoWork work = MakeGetAddrInfoWork(ctx);

    LOG_DEBUG(Service,
              "called. use_nsd_resolve={}, cancel_handle=0x{:08X}, "
              "process_id=0x{:016X} node='{}' service='{}' num_hints={}",
              work.use_nsd_resolve, work.cancel_handle, work.process_id,
              work.node.empty() ? "(null)" : work.node.data(),
              work.service.empty() ? "(null)" : work.service.data(), work.hints.size());

    work.node = system.OnlineInitiator().ResolveUrl(work.node, work.use_nsd_resolve != 0);

    ExecuteWork(ctx, std::move(work));
}

void SFDNSRES::GetHostByNameRequestWithOptions(Kernel::HLERequestContext& ctx) {
    GetHostByNameWithOptionsWork work{MakeGetHostByNameWork(ctx)};

    LOG_WARNING(Service,
                "(STUBBED) called. use_nsd_resolve={}, cancel_handle=0x{:x}, process_id=0x{:016X} "
                "hostname='{}' options_len={}",
                work.use_nsd_resolve, work.cancel_handle, work.process_id, work.hostname,
                ctx.GetReadBufferSize(1));

    work.hostname = system.OnlineInitiator().ResolveUrl(work.hostname, work.use_nsd_resolve != 0);

    ExecuteWork(ctx, std::move(work));
}

void SFDNSRES::GetAddrInfoRequestWithOptions(Kernel::HLERequestContext& ctx) {
    GetAddrInfoWithOptionsWork work{MakeGetAddrInfoWork(ctx)};

    LOG_WARNING(Service,
                "(STUBBED) called. use_nsd_resolve={}, cancel_handle=0x{:08X}, "
                "process_id=0x{:016X} node='{}' service='{}' num_hints={} options_len={}",
                work.use_nsd_resolve, work.cancel_handle, work.process_id,
                work.node.empty() ? "(null)" : work.node.data(),
                work.service.empty() ? "(null)" : work.service.data(), work.hints.size(),
                ctx.GetReadBufferSize(3));

    work.node = system.OnlineInitiator().ResolveUrl(work.node, work.use_nsd_resolve != 0);

    ExecuteWork(ctx, std::move(work));
}

void SFDNSRES::GetCancelHandleRequest(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(++current_cancel_handle);
}

template <typename Work>
void SFDNSRES::ExecuteWork(Kernel::HLERequestContext& ctx, Work work) {
    work.Execute(this);
    work.Response(ctx);
}

SFDNSRES::SFDNSRES(Core::System& system_)
    : ServiceFramework{system_, "sfdnsres"} {
    static const FunctionInfo functions[] = {
        {0, nullptr, "SetDnsAddressesPrivateRequest"},
        {1, nullptr, "GetDnsAddressPrivateRequest"},
        {2, &SFDNSRES::GetHostByNameRequest, "GetHostByNameRequest"},
        {3, nullptr, "GetHostByAddrRequest"},
        {4, nullptr, "GetHostStringErrorRequest"},
        {5, nullptr, "GetGaiStringErrorRequest"},
        {6, &SFDNSRES::GetAddrInfoRequest, "GetAddrInfoRequest"},
        {7, nullptr, "GetNameInfoRequest"},
        {8, &SFDNSRES::GetCancelHandleRequest, "GetCancelHandleRequest"},
        {9, nullptr, "CancelRequest"},
        {10, &SFDNSRES::GetHostByNameRequestWithOptions, "GetHostByNameRequestWithOptions"},
        {11, nullptr, "GetHostByAddrRequestWithOptions"},
        {12, &SFDNSRES::GetAddrInfoRequestWithOptions, "GetAddrInfoRequestWithOptions"},
        {13, nullptr, "GetNameInfoRequestWithOptions"},
        {14, nullptr, "ResolverSetOptionRequest"},
        {15, nullptr, "ResolverGetOptionRequest"},
    };
    RegisterHandlers(functions);
}

SFDNSRES::~SFDNSRES() = default;

} // namespace Service::Sockets
