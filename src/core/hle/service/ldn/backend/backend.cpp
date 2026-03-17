// Copyright 2020 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <atomic>
#include <mutex>
#include <random>
#include <span>
#include <thread>

#include "common/assert.h"
#include "core/core.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/ldn/backend/backend.h"
#include "core/network/sockets.h"

#pragma optimize("", off)

namespace Service::LDN {

namespace {

constexpr u8 FAKE_SSID_LEN = 8;
constexpr std::array<u8, 33> FAKE_SSID{'y', 'u', 'z', 'u', 'N', 'e', 't'};

constexpr std::string_view Name(State state) {
    switch (state) {
    case State::None:
        return "None";
    case State::Initialized:
        return "Initialized";
    case State::AccessPointOpened:
        return "AccessPointOpened";
    case State::AccessPointCreated:
        return "AccessPointCreated";
    case State::StationOpened:
        return "StationOpened";
    case State::StationConnected:
        return "StationConnected";
    case State::Error:
        return "Error";
    }
    return "Unknown";
}

} // Anonymous namespace

LanStation::LanStation(s8 node_id_) : node_id{node_id_} {}

LanStation::~LanStation() = default;

void LanStation::OverrideInfo(NodeInfo* node_info) const {
    node_info->id = node_id;
    node_info->is_connected = IsConnected() ? 1 : 0;
}

bool LanImpl::Initialize() {
    ASSERT(state == State::None);

    if (!InitUDP()) {
        return false;
    }

    poll_thread = std::thread(&LanImpl::WorkerLoop, this);

    state = State::Initialized;

    return true;
}

bool LanImpl::Finalize() {
    if (state != State::None) {
        stop.store(true, std::memory_order_relaxed);
        poll_thread.join();
        udp.Close();
        tcp.Close();
        // ResetStations();
    }

    state = State::None;
    return true;
}

State LanImpl::GetState() const {
    return state;
}

std::optional<NetworkInfo> LanImpl::GetNetworkInfo() const {
    if (state == State::StationConnected || state == State::AccessPointCreated) {
        return network_info;
    }
    LOG_ERROR(Service_LDN, "called with invalid state={}", Name(state));
    return {};
}

std::optional<std::pair<IPv4Address, Netmask>> LanImpl::GetIpv4Address() const {
    auto [address, err] = Network::GetHostIPv4Address();
    ASSERT(err == Network::Errno::SUCCESS);

    std::reverse(address.begin(), address.end());

    // TODO: Read netmask
    Netmask netmask{255, 255, 255, 0};
    std::reverse(netmask.begin(), netmask.end());

    return std::make_pair(address, netmask);
}

std::optional<DisconnectReason> LanImpl::GetDisconnectReason() const {
    return disconnect_reason;
}

std::optional<SecurityParameter> LanImpl::GetSecurityParameter() const {
    return SecurityParameter{
        .data = network_info.security_data,
        .session_id = network_info.session_id,
    };
}

std::optional<std::vector<NetworkInfo>> LanImpl::Scan(s16 channel, const ScanFilter& scan_filter) {
    // UNIMPLEMENTED();
    return std::vector<NetworkInfo>{};
}

bool LanImpl::OpenAccessPoint() {
    ASSERT(state == State::Initialized);

    std::scoped_lock lock{poll_mutex};
    if (tcp.IsOpened() && tcp.Close() != Network::Errno::SUCCESS) {
        return false;
    }

    // ResetStations

    state = State::AccessPointOpened;
    return true;
}

bool LanImpl::CloseAccessPoint() {
    ASSERT(state == State::AccessPointOpened || state == State::AccessPointCreated);

    std::scoped_lock lock{poll_mutex};
    if (tcp.IsOpened() && tcp.Close() != Network::Errno::SUCCESS) {
        return false;
    }

    // ResetStations

    state = State::Initialized;
    return true;
}

bool LanImpl::CreateNetwork(const SecurityConfig& security_config, const UserConfig& user_config,
                            const NetworkConfig& network_config) {
    ASSERT(state == State::AccessPointOpened);

    if (!InitTCP()) {
        return false;
    }

    const auto [address, err] = Network::GetHostIPv4Address();
    ASSERT(err == Network::Errno::SUCCESS);

    network_info = NetworkInfo{
        .intent_id = network_config.intent_id,
        .mac_address = {2, 0, address[0], address[1], address[2], address[3]},
        .ssid_length = FAKE_SSID_LEN,
        .ssid_sz = FAKE_SSID,
        .channel = static_cast<s16>(network_config.channel == 0 ? 6 : network_config.channel),
        .link_level = 3,
        .network_mode = NetworkMode::LDN,
        .security_config_type = security_config.type,
        .max_participants = network_config.max_participants,
    };

    // Generate a random session id
    std::array<u32, 4> random_id;
    std::generate(random_id.begin(), random_id.end(), random_engine);
    std::memcpy(network_info.session_id.data(), random_id.data(), sizeof(random_id));

    for (size_t i = 0; i < network_info.nodes.size(); ++i) {
        network_info.nodes[i] = NodeInfo{
            .id = static_cast<s8>(i),
            .is_connected = 0,
        };
    }
    network_info.nodes[0] = NodeInfo{
        .ipv4_address = address,
        .mac_address = network_info.mac_address,
        .id = 0,
        .is_connected = 1,
        .nickname_sz = user_config.nickname,
        .local_communication_version = network_config.local_communication_version,
    };

    state = State::AccessPointCreated;

    InitNodeStateChange();
    network_info.nodes[0].is_connected = 1;
    UpdateNodes();

    return true;
}

bool LanImpl::SetAdvertiseData(std::span<const u8> advertise_data) {
    ASSERT(advertise_data.size() < network_info.advertise_data.size());

    network_info.advertise_data_size = static_cast<u16>(advertise_data.size());
    std::copy(advertise_data.begin(), advertise_data.end(), network_info.advertise_data.data());

    UpdateNodes();

    return true;
}

bool LanImpl::OpenStation() {
    // UNIMPLEMENTED();
    state = State::StationOpened;
    return true;
}

bool LanImpl::CloseStation() {
    // UNIMPLEMENTED();
    state = State::Initialized;
    return true;
}

bool LanImpl::Connect() {
    UNIMPLEMENTED();
    return true;
}

bool LanImpl::Disconnect() {
    UNIMPLEMENTED();
    return true;
}

void LanImpl::WorkerLoop() {
    auto& system = Core::System::GetInstance();
    system.RegisterHostThread();

    std::array poll_fds{
        Network::PollFD{
            .socket = &udp,
            .events = Network::PollEvents::In,
        },
        Network::PollFD{
            .socket = &tcp,
            .events = Network::PollEvents::In,
        },
    };

    while (!stop) {
        std::unique_lock lock{poll_mutex};
        poll_cv.wait_for(lock, std::chrono::microseconds{500});

        std::span<Network::PollFD> args = poll_fds;
        if (!tcp.IsOpened()) {
            args = args.subspan(0, 1);
        }

        const s32 active_files = Network::Poll(args.size(), args.data(), 0).first;
        if (active_files < 0) {
            LOG_ERROR(Service_LDN, "Error polling sockets");
            continue;
        }

        if (udp.IsOpened()) {
            HandlePollUDP(static_cast<u16>(poll_fds[0].revents));
        }
        if (tcp.IsOpened()) {
            HandlePollTCP(static_cast<u16>(poll_fds[1].revents));
        }
    }
}

bool LanImpl::InitUDP() {
    std::scoped_lock lock{poll_mutex};

    if (udp.IsOpened()) {
        [[maybe_unused]] const Network::Errno err = udp.Close();
        ASSERT(err == Network::Errno::SUCCESS);
    }

    if (udp.Initialize(Network::Domain::INET, Network::Type::DGRAM, Network::Protocol::UDP) !=
        Network::Errno::SUCCESS) {
        return false;
    }

    static constexpr Network::SockAddrIn addr{
        .family = Network::Domain::INET,
        .ip = {0, 0, 0, 0},
        .portno = LISTEN_PORTNO,
    };
    if (udp.Bind(addr) != Network::Errno::SUCCESS) {
        return false;
    }

    if (udp.SetBroadcast(true) != Network::Errno::SUCCESS) {
        return false;
    }
    if (udp.SetReuseAddr(true) != Network::Errno::SUCCESS) {
        return false;
    }

    return true;
}

bool LanImpl::InitTCP() {
    std::scoped_lock lock{poll_mutex};

    Network::Errno err;

    if (tcp.IsOpened()) {
        err = tcp.Close();
        ASSERT(err == Network::Errno::SUCCESS);
    }

    err = tcp.Initialize(Network::Domain::INET, Network::Type::STREAM, Network::Protocol::TCP);
    ASSERT(err == Network::Errno::SUCCESS);

    static constexpr Network::SockAddrIn addr = {
        .family = Network::Domain::INET,
        .ip = {0, 0, 0, 0},
        .portno = LISTEN_PORTNO,
    };
    err = tcp.Bind(addr);
    ASSERT(err == Network::Errno::SUCCESS);

    err = tcp.Listen(10);
    ASSERT(err == Network::Errno::SUCCESS);

    err = tcp.SetReuseAddr(true);
    ASSERT(err == Network::Errno::SUCCESS);

    return true;
}

void LanImpl::HandlePollUDP(u16 revents) {
    if (revents == 0) {
        return;
    }
    LOG_INFO(Service_LDN, "Hit revents={}", revents);
}

void LanImpl::HandlePollTCP(u16 revents) {
    if (revents == 0) {
        return;
    }
    LOG_INFO(Service_LDN, "Hit revents={}", revents);
}

void LanImpl::InitNodeStateChange() {
    for (auto& node : node_changes) {
        node.state_change = 0;
    }
    for (auto& last_state : node_last_states) {
        last_state = 0; // FIXME: what's zero?
    }
}

void LanImpl::UpdateNodes() {
    size_t num_connected = 0;
    for (size_t i = 0; i < stations.size(); ++i) {
        num_connected += stations[i].IsConnected() ? 1 : 0;
        stations[i].OverrideInfo(&network_info.nodes[stations[i].Id()]);
    }

    // Don't forget to count localhost as a participant
    network_info.num_participants = static_cast<u8>(num_connected + 1);

    for (LanStation& station : stations) {
        if (station.Status() == NodeStatus::Connected) {
            // TODO: SendPacket
        }
    }

    OnNetworkInfoChanged();
}

bool LanImpl::TestNodeStateChanged() {
    auto& nodes = network_info.nodes;
    bool changed = false;

    for (size_t i = 0; i < MAX_NUM_NODES; ++i) {
        if (nodes[i].is_connected == node_last_states[i]) {
            continue;
        }
        if (nodes[i].is_connected) {
            node_changes[i].state_change |= NODE_STATE_CHANGE_CONNECT;
        } else {
            node_changes[i].state_change |= NODE_STATE_CHANGE_DISCONNECT;
        }
        node_last_states[i] = nodes[i].is_connected;
        changed = true;
    }
    return changed;
}

void LanImpl::OnNetworkInfoChanged() {
    if (TestNodeStateChanged()) {
        LOG_INFO(Service_LDN, "Signal event");
        //        network_info_event.writable->Signal();
    }
}

} // namespace Service::LDN
