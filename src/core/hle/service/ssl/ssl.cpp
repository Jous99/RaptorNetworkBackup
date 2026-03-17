// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <openssl/ssl.h>

#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/hle_ipc.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/sockets/bsd.h"
#include "core/hle/service/ssl/ssl.h"
#include "core/network/sockets.h"

namespace Service::SSL {

namespace {

constexpr u32 SSL_VERSION_AUTO = 1u << 0;
constexpr u32 SSL_VERSION_V10 = 1u << 3;
constexpr u32 SSL_VERSION_V11 = 1u << 4;
constexpr u32 SSL_VERSION_V12 = 1u << 5;

constexpr ResultCode RESULT_WOULDBLOCK = ResultCode(ErrorModule::SSL, 204);

enum class IoMode : u32 {
    Blocking = 1,
    NonBlocking = 2,
};

const SSL_METHOD* MethodFromVersion(u32 version) {
    if (version == SSL_VERSION_AUTO) {
        LOG_WARNING(Service_SSL, "Untested SSL version auto selection");
        return TLSv1_2_method();
    }
    if (version == SSL_VERSION_V12) {
        return TLSv1_2_method();
    }
    UNIMPLEMENTED_MSG("Unimplemented version={}", static_cast<int>(version));
    return nullptr;
}

ResultCode MakeResult(::SSL* ssl_connection, int err) {
    if (err >= 0) {
        return RESULT_SUCCESS;
    }
    switch (const int code = SSL_get_error(ssl_connection, err)) {
    case SSL_ERROR_WANT_READ:
        LOG_ERROR(Service_SSL, "Unexpected non-blocking error");
        return RESULT_WOULDBLOCK;
    default:
        UNIMPLEMENTED_MSG("Unimplemented SSL error={}", code);
        return RESULT_SUCCESS;
    }
}

[[nodiscard]] bool IsReady(Sockets::BSD::FileDescriptor* descriptor, u16 event) {
    Network::PollFD pollfd{
        .socket = descriptor->socket.get(),
        .events = static_cast<Network::PollEvents>(event),
        .revents = static_cast<Network::PollEvents>(0),
    };
    const auto [poll_ret, poll_err] = Network::Poll(1, &pollfd, 0);
    UNIMPLEMENTED_IF(poll_err != Network::Errno::SUCCESS);
    UNIMPLEMENTED_IF(poll_ret < 0);
    if (poll_ret == 0) {
        return false;
    }
    ASSERT(static_cast<u16>(pollfd.revents) == event);
    return true;
}

class ISslConnection final : public ServiceFramework<ISslConnection> {
public:
    explicit ISslConnection(Core::System& system, std::shared_ptr<Sockets::BSD> bsd_u_,
                            u32* num_connections_, ::SSL* ssl_connection_)
        : ServiceFramework{system, "ISslConnection"}, bsd_u{bsd_u_},
          num_connections{num_connections_}, ssl_connection{ssl_connection_} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &ISslConnection::SetSocketDescriptor, "SetSocketDescriptor"},
            {1, &ISslConnection::SetHostName, "SetHostName"},
            {2, &ISslConnection::SetVerifyOption, "SetVerifyOption"},
            {3, &ISslConnection::SetIoMode, "SetIoMode"},
            {4, nullptr, "GetSocketDescriptor"},
            {5, nullptr, "GetHostName"},
            {6, nullptr, "GetVerifyOption"},
            {7, nullptr, "GetIoMode"},
            {8, &ISslConnection::DoHandshake, "DoHandshake"},
            {9, nullptr, "DoHandshakeGetServerCert"},
            {10, &ISslConnection::Read, "Read"},
            {11, &ISslConnection::Write, "Write"},
            {12, &ISslConnection::Pending, "Pending"},
            {13, &ISslConnection::Peek, "Peek"},
            {14, nullptr, "Poll"},
            {15, nullptr, "GetVerifyCertError"},
            {16, nullptr, "GetNeededServerCertBufferSize"},
            {17, &ISslConnection::SetSessionCacheMode, "SetSessionCacheMode"},
            {18, nullptr, "GetSessionCacheMode"},
            {19, nullptr, "FlushSessionCache"},
            {20, nullptr, "SetRenegotiationMode"},
            {21, nullptr, "GetRenegotiationMode"},
            {22, &ISslConnection::SetOption, "SetOption"},
            {23, nullptr, "GetOption"},
            {24, nullptr, "GetVerifyCertErrors"},
            {25, nullptr, "GetCipherInfo"},
            {26, &ISslConnection::SetNextAlpnProto, "SetNextAlpnProto"},
            {27, nullptr, "GetNextAlpnProto"},
        };
        // clang-format on

        RegisterHandlers(functions);

        ++*num_connections;
    }

    ~ISslConnection() {
        SSL_free(ssl_connection);

        --*num_connections;
    }

private:
    void SetSocketDescriptor(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const s32 fd = rp.Pop<s32>();

        SSL_set_mode(ssl_connection, SSL_MODE_AUTO_RETRY);

        LOG_WARNING(Service_SSL, "(STUBBED) called. fd={}", fd);

        descriptor = bsd_u->GetFileDescriptor(fd);
        ASSERT(descriptor);

        const auto handle = descriptor->socket->fd;
        [[maybe_unused]] const int err = SSL_set_fd(ssl_connection, static_cast<int>(handle));
        ASSERT(err == 1);

        // TODO: Set connect state somewhere else
        SSL_set_connect_state(ssl_connection);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        // TODO: This socket will be closed from bsd. Investigate what should be returned here.
        rb.Push<s32>(255);
    }

    void SetHostName(Kernel::HLERequestContext& ctx) {
        const std::vector<u8> hostname_vector = ctx.ReadBuffer();
        std::string hostname(reinterpret_cast<const char*>(hostname_vector.data()),
                             hostname_vector.size());

        LOG_WARNING(Service_SSL, "(STUBBED) called hostname=\"{}\"", hostname);

        [[maybe_unused]] const long int err =
            SSL_set_tlsext_host_name(ssl_connection, hostname.c_str());
        ASSERT(err == 1);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void SetVerifyOption(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u32 verify_option = rp.Pop<u32>();

        LOG_WARNING(Service_SSL, "(STUBBED) called. verify_option=0x{:x}", verify_option);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void SetIoMode(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u32 io_mode_value = rp.Pop<u32>();

        LOG_WARNING(Service_SSL, "(STUBBED) called. io_mode=0x{:x}", io_mode_value);

        io_mode = static_cast<IoMode>(io_mode_value);
        ASSERT(io_mode == IoMode::Blocking || io_mode == IoMode::NonBlocking);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void DoHandshake(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_SSL, "(STUBBED) called");

        UNIMPLEMENTED_IF(io_mode != IoMode::NonBlocking);

        ResultCode result = RESULT_SUCCESS;

        const int err = SSL_do_handshake(ssl_connection);
        if (err == 1) {
            LOG_INFO(Service_SSL, "Successful handshake");
        } else if (err == 0) {
            UNIMPLEMENTED_MSG("Unhandled error type");
        } else {
            result = MakeResult(ssl_connection, err);
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result);
    }

    void Read(Kernel::HLERequestContext& ctx) {
        const size_t len = ctx.GetWriteBufferSize();

        LOG_WARNING(Service_SSL, "(STUBBED) called. len={}", len);

        UNIMPLEMENTED_IF(io_mode != IoMode::NonBlocking);

        // Make sure we have data to read to avoid blocking
        if (!IsReady(descriptor, static_cast<u16>(Network::PollEvents::In))) {
            LOG_DEBUG(Service_SSL, "Would block emitted");
            IPC::ResponseBuilder rb{ctx, 3};
            rb.Push(RESULT_WOULDBLOCK);
            rb.Push<s32>(-1);
            return;
        }

        // This is guaranteed to read without waiting
        std::vector<u8> buffer(len);
        const int ret = SSL_read(ssl_connection, buffer.data(), static_cast<int>(len));

        ctx.WriteBuffer(buffer);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(MakeResult(ssl_connection, ret));
        rb.Push<s32>(ret);
    }

    void Write(Kernel::HLERequestContext& ctx) {
        const std::vector<u8> buffer = ctx.ReadBuffer(0);

        LOG_WARNING(Service_SSL, "(STUBBED) called. len={}", buffer.size());

        UNIMPLEMENTED_IF(io_mode != IoMode::NonBlocking);

        // Make sure we have data to write to avoid blocking
        if (!IsReady(descriptor, static_cast<u16>(Network::PollEvents::Out))) {
            LOG_DEBUG(Service_SSL, "Would block emitted");
            IPC::ResponseBuilder rb{ctx, 3};
            rb.Push(RESULT_WOULDBLOCK);
            rb.Push<s32>(-1);
            return;
        }

        // This is guaranteed to write without waiting
        const int ret = SSL_write(ssl_connection, buffer.data(), static_cast<int>(buffer.size()));

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(MakeResult(ssl_connection, ret));
        rb.Push<s32>(ret);
    }

    void Pending(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_SSL, "(STUBBED) called");

        const int ret = SSL_pending(ssl_connection);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(MakeResult(ssl_connection, ret));
        rb.Push<s32>(ret);
    }

    void Peek(Kernel::HLERequestContext& ctx) {
        const size_t len = ctx.GetWriteBufferSize();

        LOG_WARNING(Service_SSL, "(STUBBED) called. len={}", len);

        UNIMPLEMENTED_IF(io_mode != IoMode::NonBlocking);

        std::vector<u8> buffer(len);
        const int ret = SSL_peek(ssl_connection, buffer.data(), static_cast<int>(len));

        ctx.WriteBuffer(buffer);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(MakeResult(ssl_connection, ret));
        rb.Push<s32>(ret);
    }

    void SetSessionCacheMode(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u32 cache_mode = rp.Pop<u32>();

        LOG_WARNING(Service_SSL, "(STUBBED) called. cache_mode={}", cache_mode);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void SetOption(Kernel::HLERequestContext& ctx) {
        struct Parameters {
            u8 value;
            u32 option;
        };
        IPC::RequestParser rp{ctx};
        const auto parameters = rp.PopRaw<Parameters>();

        LOG_WARNING(Service_SSL, "(STUBBED) called. value={} option={}", parameters.value,
                    parameters.option);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void SetNextAlpnProto(Kernel::HLERequestContext& ctx) {
        const auto buffer = ctx.ReadBuffer();
        const auto length = buffer[0];
        if ((length + 1ull) > buffer.size()) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(RESULT_UNKNOWN);
        }

        const auto ret = SSL_set_alpn_protos(ssl_connection, buffer.data(),
                                             static_cast<unsigned int>(buffer.size()));

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(MakeResult(ssl_connection, ret));
    }

    std::shared_ptr<Sockets::BSD> bsd_u;
    u32* num_connections = nullptr;
    ::SSL* ssl_connection = nullptr;
    Sockets::BSD::FileDescriptor* descriptor = nullptr;
    IoMode io_mode = IoMode::Blocking;
};

class ISslContext final : public ServiceFramework<ISslContext> {
public:
    explicit ISslContext(Core::System& system, std::shared_ptr<Sockets::BSD> bsd_u_,
                         SSL_CTX* ssl_context_)
        : ServiceFramework{system, "ISslContext"}, bsd_u{std::move(bsd_u_)}, ssl_context{
                                                                                 ssl_context_} {
        static const FunctionInfo functions[] = {
            {0, &ISslContext::SetOption, "SetOption"},
            {1, nullptr, "GetOption"},
            {2, &ISslContext::CreateConnection, "CreateConnection"},
            {3, &ISslContext::GetConnectionCount, "GetConnectionCount"},
            {4, nullptr, "ImportServerPki"},
            {5, nullptr, "ImportClientPki"},
            {6, nullptr, "RemoveServerPki"},
            {7, nullptr, "RemoveClientPki"},
            {8, nullptr, "RegisterInternalPki"},
            {9, nullptr, "AddPolicyOid"},
            {10, nullptr, "ImportCrl"},
            {11, nullptr, "RemoveCrl"},
        };
        RegisterHandlers(functions);
    }

    ISslContext(const ISslContext&) = delete;
    ISslContext& operator=(const ISslContext&) = delete;

    ~ISslContext() {
        SSL_CTX_free(ssl_context);
    }

private:
    void SetOption(Kernel::HLERequestContext& ctx) {
        struct Parameters {
            u8 enable;
            u32 option;
        };

        IPC::RequestParser rp{ctx};
        const auto parameters = rp.PopRaw<Parameters>();

        LOG_WARNING(Service_SSL, "(STUBBED) called. enable={}, option={}", parameters.enable,
                    parameters.option);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void CreateConnection(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_SSL, "(STUBBED) called");

        ::SSL* ssl_connection = SSL_new(ssl_context);
        ASSERT(ssl_connection);

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<ISslConnection>(system, bsd_u, &num_connections, ssl_connection);
    }

    void GetConnectionCount(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_SSL, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push(num_connections);
    }

    std::shared_ptr<Sockets::BSD> bsd_u;
    SSL_CTX* ssl_context = nullptr;
    u32 num_connections = 0;
};

class SSL final : public ServiceFramework<SSL> {
public:
    explicit SSL(Core::System& system_, std::shared_ptr<Sockets::BSD> bsd_u_)
        : ServiceFramework{system_, "ssl"}, bsd_u{std::move(bsd_u_)} {
        // Initialize host SSL (this never fails)
        (void)SSL_library_init();

        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &SSL::CreateContext, "CreateContext"},
            {1, nullptr, "GetContextCount"},
            {2, nullptr, "GetCertificates"},
            {3, nullptr, "GetCertificateBufSize"},
            {4, nullptr, "DebugIoctl"},
            {5, &SSL::SetInterfaceVersion, "SetInterfaceVersion"},
            {6, nullptr, "FlushSessionCache"},
            {7, nullptr, "SetDebugOption"},
            {8, nullptr, "GetDebugOption"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void CreateContext(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u32 version = rp.Pop<u32>();
        const u64 process_id = rp.Pop<u64>();

        LOG_WARNING(Service_SSL, "(STUBBED) called. version={} process_id=0x{:x}", version,
                    process_id);

        SSL_CTX* const ssl_context = SSL_CTX_new(MethodFromVersion(version));
        ASSERT(ssl_context);

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<ISslContext>(system, bsd_u, ssl_context);
    }

    void SetInterfaceVersion(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_SSL, "called");

        IPC::RequestParser rp{ctx};
        ssl_version = rp.Pop<u32>();

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    std::shared_ptr<Sockets::BSD> bsd_u;
    u32 ssl_version = 0;
};

} // Anonymous namespace

void InstallInterfaces(SM::ServiceManager& service_manager, Core::System& system) {
    std::shared_ptr<Sockets::BSD> bsd_u = service_manager.GetService<Sockets::BSD>("bsd:u");

    std::make_shared<SSL>(system, bsd_u)->InstallAsService(service_manager);
}

} // namespace Service::SSL
