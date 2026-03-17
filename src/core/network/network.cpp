// Copyright 2020 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>
#include <vector>
#include "common/common_funcs.h"

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS // gethostname
#include <winsock2.h>
#include <ws2tcpip.h>
#elif YUZU_UNIX
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#error "Unimplemented platform"
#endif

#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "core/network/network.h"
#include "core/network/sockets.h"

namespace Network {

namespace {

#ifdef _WIN32

using socklen_t = int;

void Initialize() {
    WSADATA wsa_data;
    (void)WSAStartup(MAKEWORD(2, 2), &wsa_data);
}

void Finalize() {
    WSACleanup();
}

constexpr IPv4Address TranslateIPv4(in_addr addr) {
    auto& bytes = addr.S_un.S_un_b;
    return IPv4Address{bytes.s_b1, bytes.s_b2, bytes.s_b3, bytes.s_b4};
}

sockaddr TranslateFromSockAddrIn(SockAddrIn input) {
    sockaddr_in result;
    std::memset(&result, 0, sizeof(result));

#if YUZU_UNIX
    result.sin_len = sizeof(result);
#endif

    switch (static_cast<Domain>(input.family)) {
    case Domain::INET:
        result.sin_family = AF_INET;
        break;
    default:
        UNIMPLEMENTED_MSG("Unhandled sockaddr_data family={}", input.family);
        result.sin_family = AF_INET;
        break;
    }

    result.sin_port = htons(input.portno);

    auto& ip = result.sin_addr.S_un.S_un_b;
    ip.s_b1 = input.ip[0];
    ip.s_b2 = input.ip[1];
    ip.s_b3 = input.ip[2];
    ip.s_b4 = input.ip[3];

    sockaddr addr;
    std::memcpy(&addr, &result, sizeof(addr));
    return addr;
}

LINGER MakeLinger(bool enable, u32 linger_value) {
    ASSERT(linger_value <= std::numeric_limits<u_short>::max());

    LINGER value;
    value.l_onoff = enable ? 1 : 0;
    value.l_linger = static_cast<u_short>(linger_value);
    return value;
}

bool EnableNonBlock(SOCKET fd, bool enable) {
    u_long value = enable ? 1 : 0;
    return ioctlsocket(fd, FIONBIO, &value) != SOCKET_ERROR;
}

Errno TranslateNativeError(int e) {
    switch (e) {
    case WSAEBADF:
        return Errno::BADF;
    case WSAEINVAL:
        return Errno::INVAL;
    case WSAEMFILE:
        return Errno::MFILE;
    case WSAENOTCONN:
        return Errno::NOTCONN;
    case WSAEWOULDBLOCK:
        return Errno::AGAIN;
    case WSAECONNREFUSED:
        return Errno::CONNREFUSED;
    case WSAEHOSTUNREACH:
        return Errno::HOSTUNREACH;
    case WSAENETDOWN:
        return Errno::NETDOWN;
    case WSAENETUNREACH:
        return Errno::NETUNREACH;
    default:
        return Errno::OTHER;
    }
}

#elif YUZU_UNIX // ^ _WIN32 v YUZU_UNIX

using SOCKET = int;
using WSAPOLLFD = pollfd;
using ULONG = u64;

constexpr SOCKET INVALID_SOCKET = -1;
constexpr SOCKET SOCKET_ERROR = -1;

constexpr int SD_RECEIVE = SHUT_RD;
constexpr int SD_SEND = SHUT_WR;
constexpr int SD_BOTH = SHUT_RDWR;

void Initialize() {}

void Finalize() {}

constexpr IPv4Address TranslateIPv4(in_addr addr) {
    const u32 bytes = addr.s_addr;
    return IPv4Address{static_cast<u8>(bytes), static_cast<u8>(bytes >> 8),
                       static_cast<u8>(bytes >> 16), static_cast<u8>(bytes >> 24)};
}

sockaddr TranslateFromSockAddrIn(SockAddrIn input) {
    sockaddr_in result;
    std::memset(&result, 0, sizeof(result));

    switch (static_cast<Domain>(input.family)) {
    case Domain::INET:
        result.sin_family = AF_INET;
        break;
    default:
        UNIMPLEMENTED_MSG("Unhandled sockaddr_data family={}", input.family);
        result.sin_family = AF_INET;
        break;
    }

    result.sin_port = htons(input.portno);

    result.sin_addr.s_addr = input.ip[0] | input.ip[1] << 8 | input.ip[2] << 16 | input.ip[3] << 24;

    sockaddr addr;
    std::memcpy(&addr, &result, sizeof(addr));
    return addr;
}

int WSAPoll(WSAPOLLFD* fds, ULONG nfds, int timeout) {
    return poll(fds, static_cast<nfds_t>(nfds), timeout);
}

int closesocket(SOCKET fd) {
    return close(fd);
}

linger MakeLinger(bool enable, u32 linger_value) {
    linger value;
    value.l_onoff = enable ? 1 : 0;
    value.l_linger = linger_value;
    return value;
}

bool EnableNonBlock(int fd, bool enable) {
    int flags = fcntl(fd, F_GETFD);
    if (flags == -1) {
        return false;
    }
    if (enable) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(fd, F_SETFD, flags) == 0;
}

Errno TranslateNativeError(int e) {
    switch (e) {
    case EBADF:
        return Errno::BADF;
    case EINVAL:
        return Errno::INVAL;
    case EMFILE:
        return Errno::MFILE;
    case ENOTCONN:
        return Errno::NOTCONN;
    case EAGAIN:
        return Errno::AGAIN;
    case ECONNREFUSED:
        return Errno::CONNREFUSED;
    case EHOSTUNREACH:
        return Errno::HOSTUNREACH;
    case ENETDOWN:
        return Errno::NETDOWN;
    case ENETUNREACH:
        return Errno::NETUNREACH;
    default:
        return Errno::OTHER;
    }
}

#endif

Errno GetAndLogLastError() {
#ifdef _WIN32
    int e = WSAGetLastError();
#else
    int e = errno;
#endif
    LOG_ERROR(Network, "Socket operation error: {}", NativeErrorToString(e));
    return TranslateNativeError(e);
}

int TranslateDomain(Domain domain) {
    switch (domain) {
    case Domain::INET:
        return AF_INET;
    default:
        UNIMPLEMENTED_MSG("Unimplemented domain={}", domain);
        return 0;
    }
}

int TranslateType(Type type) {
    switch (type) {
    case Type::UNSPECIFIED:
        return 0;
    case Type::STREAM:
        return SOCK_STREAM;
    case Type::DGRAM:
        return SOCK_DGRAM;
    default:
        UNIMPLEMENTED_MSG("Unimplemented type={}", type);
        return 0;
    }
}

Type TranslateType(int type) {
    switch (type) {
    case 0:
        return Type::UNSPECIFIED;
    case SOCK_STREAM:
        return Type::STREAM;
    case SOCK_DGRAM:
        return Type::DGRAM;
    default:
        UNIMPLEMENTED_MSG("Unimplemented type={}", type);
        return {};
    }
}

int TranslateProtocol(Protocol protocol) {
    switch (protocol) {
    case Protocol::UNSPECIFIED:
        return 0;
    case Protocol::TCP:
        return IPPROTO_TCP;
    case Protocol::UDP:
        return IPPROTO_UDP;
    default:
        UNIMPLEMENTED_MSG("Unimplemented protocol={}", protocol);
        return 0;
    }
}

Protocol TranslateProtocol(int protocol) {
    switch (protocol) {
    case 0:
        return Protocol::UNSPECIFIED;
    case IPPROTO_TCP:
        return Protocol::TCP;
    case IPPROTO_UDP:
        return Protocol::UDP;
    default:
        UNIMPLEMENTED_MSG("Unimplemented protocol={}", protocol);
        return {};
    }
}

Domain TranslateFamily(int family) {
    switch (family) {
    case AF_INET:
        return Domain::INET;
    default:
        UNIMPLEMENTED_MSG("Unhandled sockaddr_data family={}", family);
        return Domain::INET;
    }
}

SockAddrIn TranslateToSockAddrIn(sockaddr input_) {
    sockaddr_in input;
    std::memcpy(&input, &input_, sizeof(input));

    return SockAddrIn{
        .family = TranslateFamily(input.sin_family),
        .ip = TranslateIPv4(input.sin_addr),
        .portno = ntohs(input.sin_port),
    };
}

short TranslatePollEvents(PollEvents events) {
    short result = 0;

    if (True(events & PollEvents::In)) {
        events &= ~PollEvents::In;
        result |= POLLIN;
    }
    if (True(events & PollEvents::Pri)) {
        events &= ~PollEvents::Pri;
#ifdef _WIN32
        LOG_WARNING(Service, "Winsock doesn't support POLLPRI");
#else
        result |= POLLPRI;
#endif
    }
    if (True(events & PollEvents::Out)) {
        events &= ~PollEvents::Out;
        result |= POLLOUT;
    }
    if (True(events & PollEvents::Rdnorm)) {
        events &= ~PollEvents::Rdnorm;
        result |= POLLRDNORM;
    }
    if (True(events & PollEvents::Rdband)) {
        events &= ~PollEvents::Rdband;
#ifdef _WIN32
        LOG_WARNING(Service, "Winsock doesn't support POLLRDBAND");
#else
        result |= POLLRDBAND;
#endif
    }
    if (True(events & PollEvents::Wrband)) {
        events &= ~PollEvents::Wrband;
#ifdef _WIN32
        LOG_WARNING(Service, "Winsock doesn't support POLLWRBAND");
#else
        result |= POLLWRBAND;
#endif
    }

    UNIMPLEMENTED_IF_MSG((u16)events != 0, "Unhandled guest events=0x{:x}", (u16)events);

    return result;
}

PollEvents TranslatePollRevents(short revents) {
    PollEvents result{};
    const auto translate = [&result, &revents](short host, PollEvents guest) {
        if ((revents & host) != 0) {
            revents &= static_cast<short>(~host);
            result |= guest;
        }
    };

    translate(POLLIN, PollEvents::In);
    translate(POLLPRI, PollEvents::Pri);
    translate(POLLOUT, PollEvents::Out);
    translate(POLLERR, PollEvents::Err);
    translate(POLLHUP, PollEvents::Hup);
    translate(POLLRDNORM, PollEvents::Rdnorm);
    translate(POLLRDBAND, PollEvents::Rdband);
    translate(POLLWRBAND, PollEvents::Wrband);

    UNIMPLEMENTED_IF_MSG(revents != 0, "Unhandled host revents=0x{:x}", revents);

    return result;
}

std::pair<HostEnt, Errno> TranslateHostEnt(hostent* info) {
    HostEnt result;

    result.name = info->h_name;

    for (char** alias = info->h_aliases; *alias; ++alias) {
        result.aliases.push_back(*alias);
    }

    switch (info->h_addrtype) {
    case AF_INET:
        ASSERT(info->h_length == sizeof(IPv4Address));
        result.addr_type = Domain::INET;
        for (char** data = info->h_addr_list; *data; ++data) {
            in_addr addr;
            std::memcpy(&addr, *data, sizeof(addr));
            result.addr_list.push_back(TranslateIPv4(addr));
        }
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented addr_type={}", info->h_addrtype);
        result.addr_type = Domain::INET;
        break;
    }

    return {result, Errno::SUCCESS};
}

template <typename T>
Errno SetSockOpt(SOCKET fd, int option, T value) {
    const int result =
        setsockopt(fd, SOL_SOCKET, option, reinterpret_cast<const char*>(&value), sizeof(value));
    if (result != SOCKET_ERROR) {
        return Errno::SUCCESS;
    }
    return GetAndLogLastError();
}

AddrInfo TranslateToAddrInfo(addrinfo* input) {
    ASSERT(input->ai_flags == 0);
    return AddrInfo{
        .flags = 0,
        .family = TranslateFamily(input->ai_family),
        .socket_type = TranslateType(input->ai_socktype),
        .protocol = TranslateProtocol(input->ai_protocol),
        .addr = TranslateToSockAddrIn(*input->ai_addr),
        .canonname = input->ai_canonname ? input->ai_canonname : "",
    };
}

struct HostAddrInfo {
    std::string canonname;
    std::unique_ptr<sockaddr> sockaddr_data;
    addrinfo addrinfo_data;
};

HostAddrInfo TranslateToHostAddrInfo(const AddrInfo& input) {
    ASSERT(input.flags == 0);
    ASSERT(input.family == Domain::INET);

    HostAddrInfo result = {
        .canonname = input.canonname,
        .addrinfo_data =
            {
                .ai_flags = 0,
                .ai_family = TranslateDomain(input.family),
                .ai_socktype = TranslateType(input.socket_type),
                .ai_protocol = TranslateProtocol(input.protocol),
                .ai_canonname = result.canonname.empty() ? nullptr : result.canonname.data(),
            },
    };
    if (input.addr.ip != IPv4Address{0, 0, 0, 0} || input.addr.portno != 0) {
        result.sockaddr_data = std::make_unique<sockaddr>(TranslateFromSockAddrIn(input.addr));
        result.addrinfo_data.ai_addrlen = sizeof(sockaddr);
        result.addrinfo_data.ai_addr = result.sockaddr_data.get();
    }

    return result;
}

} // Anonymous namespace

NetworkInstance::NetworkInstance() {
    Initialize();
}

NetworkInstance::~NetworkInstance() {
    Finalize();
}

std::pair<IPv4Address, Errno> GetHostIPv4Address() {
    std::array<char, 256> name{};
    if (gethostname(name.data(), static_cast<int>(name.size()) - 1) == SOCKET_ERROR) {
        return {IPv4Address{}, GetAndLogLastError()};
    }

    hostent* const ent = gethostbyname(name.data());
    if (!ent) {
        return {IPv4Address{}, GetAndLogLastError()};
    }
    if (ent->h_addr_list == nullptr) {
        UNIMPLEMENTED_MSG("No addr provided in hostent->h_addr_list");
        return {IPv4Address{}, Errno::SUCCESS};
    }
    if (ent->h_length != sizeof(in_addr)) {
        UNIMPLEMENTED_MSG("Unexpected size={} in hostent->h_length", ent->h_length);
    }

    in_addr addr;
    std::memcpy(&addr, ent->h_addr_list[0], sizeof(addr));
    return {TranslateIPv4(addr), Errno::SUCCESS};
}

std::pair<HostEnt, Errno> GetHostByName(const char* name) {
    hostent* const info = gethostbyname(name);
    if (info == nullptr) {
        UNIMPLEMENTED_MSG("Unhandled gethostbyname error");
        return {HostEnt{}, Errno::SUCCESS};
    }
    return TranslateHostEnt(info);
}

std::pair<HostEnt, Errno> GetHostByAddr(const char* addr, int len, Domain type) {
    hostent* const info = gethostbyaddr(addr, len, TranslateDomain(type));
    if (info == nullptr) {
        UNIMPLEMENTED_MSG("Unhandled gethostbyname error");
        return {HostEnt{}, Errno::SUCCESS};
    }
    return TranslateHostEnt(info);
}

std::pair<std::vector<AddrInfo>, Errno> GetAddressInfo(const char* node, const char* service,
                                                       const std::vector<AddrInfo>& hints) {
    const size_t len = hints.size();
    std::vector<HostAddrInfo> host_hints(len);
    for (size_t i = 0; i < len; ++i) {
        host_hints[i] = TranslateToHostAddrInfo(hints[i]);
        if (i + 1 < len) {
            host_hints[i].addrinfo_data.ai_next = &host_hints[i + 1].addrinfo_data;
        }
    }

    addrinfo* linked_list;
    int err;
    if (hints.empty()) {
        err = getaddrinfo(node, service, nullptr, &linked_list);
    } else {
        err = getaddrinfo(node, service, &host_hints[0].addrinfo_data, &linked_list);
    }
    if (err != 0) {
        UNIMPLEMENTED_MSG("Unhandled error code={}", err);
        return {};
    }

    std::vector<AddrInfo> results;
    for (addrinfo* addr = linked_list; addr; addr = addr->ai_next) {
        results.push_back(TranslateToAddrInfo(addr));
    }
    freeaddrinfo(linked_list);
    return {std::move(results), Errno::SUCCESS};
}

std::pair<s32, Errno> Poll(size_t nfds, PollFD* pollfds, s32 timeout) {
    if (nfds == 0) {
        return {-1, Errno::SUCCESS};
    }

    std::vector<WSAPOLLFD> host_pollfds(nfds);
    std::transform(pollfds, pollfds + nfds, host_pollfds.begin(), [](PollFD fd) {
        WSAPOLLFD result;
        result.fd = fd.socket->fd;
        result.events = TranslatePollEvents(fd.events);
        result.revents = 0;
        return result;
    });

    const int result = WSAPoll(host_pollfds.data(), static_cast<ULONG>(nfds), timeout);
    if (result == 0) {
        ASSERT(std::all_of(host_pollfds.begin(), host_pollfds.end(),
                           [](WSAPOLLFD fd) { return fd.revents == 0; }));
        return {0, Errno::SUCCESS};
    }

    for (size_t i = 0; i < nfds; ++i) {
        pollfds[i].revents = TranslatePollRevents(host_pollfds[i].revents);
    }

    if (result > 0) {
        return {result, Errno::SUCCESS};
    }

    ASSERT(result == SOCKET_ERROR);

    return {-1, GetAndLogLastError()};
}

Socket::~Socket() {
    if (fd == INVALID_SOCKET) {
        return;
    }
    (void)closesocket(fd);
    fd = INVALID_SOCKET;
}

Socket::Socket(Socket&& rhs) noexcept : fd{std::exchange(rhs.fd, INVALID_SOCKET)} {}

Errno Socket::Initialize(Domain domain, Type type, Protocol protocol) {
    fd = socket(TranslateDomain(domain), TranslateType(type), TranslateProtocol(protocol));
    if (fd != INVALID_SOCKET) {
        return Errno::SUCCESS;
    }

    return GetAndLogLastError();
}

std::pair<Socket::AcceptResult, Errno> Socket::Accept() {
    sockaddr addr;
    socklen_t addrlen = sizeof(addr);
    const SOCKET new_socket = accept(fd, &addr, &addrlen);

    if (new_socket == INVALID_SOCKET) {
        return {AcceptResult{}, GetAndLogLastError()};
    }

    AcceptResult result;
    result.socket = std::make_unique<Socket>();
    result.socket->fd = new_socket;

    ASSERT(addrlen == sizeof(sockaddr_in));
    result.sockaddr_in = TranslateToSockAddrIn(addr);

    return {std::move(result), Errno::SUCCESS};
}

Errno Socket::Connect(SockAddrIn addr_in) {
    const sockaddr host_addr_in = TranslateFromSockAddrIn(addr_in);
    if (connect(fd, &host_addr_in, sizeof(host_addr_in)) != SOCKET_ERROR) {
        return Errno::SUCCESS;
    }

    return GetAndLogLastError();
}

std::pair<SockAddrIn, Errno> Socket::GetPeerName() {
    sockaddr addr;
    socklen_t addrlen = sizeof(addr);
    if (getpeername(fd, &addr, &addrlen) == SOCKET_ERROR) {
        return {SockAddrIn{}, GetAndLogLastError()};
    }

    ASSERT(addrlen == sizeof(sockaddr_in));
    return {TranslateToSockAddrIn(addr), Errno::SUCCESS};
}

std::pair<SockAddrIn, Errno> Socket::GetSockName() {
    sockaddr addr;
    socklen_t addrlen = sizeof(addr);
    if (getsockname(fd, &addr, &addrlen) == SOCKET_ERROR) {
        return {SockAddrIn{}, GetAndLogLastError()};
    }

    ASSERT(addrlen == sizeof(sockaddr_in));
    return {TranslateToSockAddrIn(addr), Errno::SUCCESS};
}

Errno Socket::Bind(SockAddrIn addr) {
    const sockaddr addr_in = TranslateFromSockAddrIn(addr);
    if (bind(fd, &addr_in, sizeof(addr_in)) != SOCKET_ERROR) {
        return Errno::SUCCESS;
    }

    return GetAndLogLastError();
}

Errno Socket::Listen(s32 backlog) {
    if (listen(fd, backlog) != SOCKET_ERROR) {
        return Errno::SUCCESS;
    }

    return GetAndLogLastError();
}

Errno Socket::Shutdown(ShutdownHow how) {
    int host_how = 0;
    switch (how) {
    case ShutdownHow::RD:
        host_how = SD_RECEIVE;
        break;
    case ShutdownHow::WR:
        host_how = SD_SEND;
        break;
    case ShutdownHow::RDWR:
        host_how = SD_BOTH;
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented flag how={}", how);
        return Errno::SUCCESS;
    }
    if (shutdown(fd, host_how) != SOCKET_ERROR) {
        return Errno::SUCCESS;
    }

    return GetAndLogLastError();
}

std::pair<s32, Errno> Socket::Recv(int flags, std::vector<u8>& message) {
    ASSERT(flags == 0);
    ASSERT(message.size() < static_cast<size_t>(std::numeric_limits<int>::max()));

    const auto result =
        recv(fd, reinterpret_cast<char*>(message.data()), static_cast<int>(message.size()), 0);
    if (result != SOCKET_ERROR) {
        return {static_cast<s32>(result), Errno::SUCCESS};
    }

    return {-1, GetAndLogLastError()};
}

std::pair<s32, Errno> Socket::RecvFrom(int flags, std::vector<u8>& message, SockAddrIn* addr) {
    ASSERT(flags == 0);
    ASSERT(message.size() < static_cast<size_t>(std::numeric_limits<int>::max()));

    sockaddr addr_in{};
    socklen_t addrlen = sizeof(addr_in);
    socklen_t* const p_addrlen = addr ? &addrlen : nullptr;
    sockaddr* const p_addr_in = addr ? &addr_in : nullptr;

    const auto result = recvfrom(fd, reinterpret_cast<char*>(message.data()),
                                 static_cast<int>(message.size()), 0, p_addr_in, p_addrlen);
    if (result != SOCKET_ERROR) {
        if (addr) {
            ASSERT(addrlen == sizeof(addr_in));
            *addr = TranslateToSockAddrIn(addr_in);
        }
        return {static_cast<s32>(result), Errno::SUCCESS};
    }

    return {-1, GetAndLogLastError()};
}

std::pair<s32, Errno> Socket::Send(const std::vector<u8>& message, int flags) {
    ASSERT(message.size() < static_cast<size_t>(std::numeric_limits<int>::max()));
    ASSERT(flags == 0);

    const auto result = send(fd, reinterpret_cast<const char*>(message.data()),
                             static_cast<int>(message.size()), 0);
    if (result != SOCKET_ERROR) {
        return {static_cast<s32>(result), Errno::SUCCESS};
    }

    return {-1, GetAndLogLastError()};
}

std::pair<s32, Errno> Socket::SendTo(u32 flags, const std::vector<u8>& message,
                                     const SockAddrIn* addr) {
    ASSERT(flags == 0);

    const sockaddr* to = nullptr;
    const int tolen = addr ? sizeof(sockaddr) : 0;
    sockaddr host_addr_in;

    if (addr) {
        host_addr_in = TranslateFromSockAddrIn(*addr);
        to = &host_addr_in;
    }

    const auto result = sendto(fd, reinterpret_cast<const char*>(message.data()),
                               static_cast<int>(message.size()), 0, to, tolen);
    if (result != SOCKET_ERROR) {
        return {static_cast<s32>(result), Errno::SUCCESS};
    }

    return {-1, GetAndLogLastError()};
}

Errno Socket::Close() {
    [[maybe_unused]] const int result = closesocket(fd);
    ASSERT(result == 0);
    fd = INVALID_SOCKET;

    return Errno::SUCCESS;
}

Errno Socket::SetLinger(bool enable, u32 linger) {
    return SetSockOpt(fd, SO_LINGER, MakeLinger(enable, linger));
}

Errno Socket::SetReuseAddr(bool enable) {
    return SetSockOpt<u32>(fd, SO_REUSEADDR, enable ? 1 : 0);
}

Errno Socket::SetBroadcast(bool enable) {
    return SetSockOpt<u32>(fd, SO_BROADCAST, enable ? 1 : 0);
}

Errno Socket::SetSndBuf(u32 value) {
    return SetSockOpt(fd, SO_SNDBUF, value);
}

Errno Socket::SetRcvBuf(u32 value) {
    return SetSockOpt(fd, SO_RCVBUF, value);
}

Errno Socket::SetSndTimeo(u32 value) {
    return SetSockOpt(fd, SO_SNDTIMEO, value);
}

Errno Socket::SetRcvTimeo(u32 value) {
    return SetSockOpt(fd, SO_RCVTIMEO, value);
}

Errno Socket::SetNonBlock(bool enable) {
    if (EnableNonBlock(fd, enable)) {
        return Errno::SUCCESS;
    }
    return GetAndLogLastError();
}

bool Socket::IsOpened() const {
    return fd != INVALID_SOCKET;
}

} // namespace Network
