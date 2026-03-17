// Copyright 2020 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <thread>
#include <type_traits>

#include "common/common_types.h"
#include "core/network/sockets.h"

namespace Service::LDN {

enum class State : u32 {
    None = 0,
    Initialized = 1,
    AccessPointOpened = 2,
    AccessPointCreated = 3,
    StationOpened = 4,
    StationConnected = 5,
    Error = 6,
};

enum class NetworkMode : u8 {
    Unknown = 0,
    Normal = 1,
    LDN = 2,
};

enum class AcceptPolicy : u8 {
    AllowAll = 0,
    DenyAll = 1,
    Blacklist = 2,
    Whitelist = 3,
};

enum class DisconnectReason : u32 {
    None = 0,
    DisconnectedByUser = 1,
    DisconnectedBySystem = 2,
    DestroyedByUser = 3,
    DestroyedBySystem = 4,
    Rejected = 5,
    SignalLost = 6,
};

constexpr u8 NODE_STATE_CHANGE_CONNECT = 1 << 0;
constexpr u8 NODE_STATE_CHANGE_DISCONNECT = 1 << 1;

struct NodeInfo {
    std::array<u8, 4> ipv4_address;
    std::array<u8, 6> mac_address;
    s8 id;
    s8 is_connected;
    std::array<u8, 32> nickname_sz;
    u16 reserved1;
    s16 local_communication_version;
    std::array<u8, 0x10> reserved2;
};
static_assert(sizeof(NodeInfo) == 0x40);
static_assert(std::has_unique_object_representations_v<NodeInfo>);

struct IntentId {
    u64 local_communication_id;
    u16 padding1;
    u16 filter;
    u32 padding2;
};
static_assert(sizeof(IntentId) == 0x10);
static_assert(std::has_unique_object_representations_v<IntentId>);

struct NetworkInfo {
    IntentId intent_id;
    std::array<u8, 16> session_id;
    std::array<u8, 6> mac_address;
    u8 ssid_length;
    std::array<u8, 33> ssid_sz;
    s16 channel;
    s8 link_level;
    NetworkMode network_mode;
    u32 padding1;
    std::array<u8, 16> security_data;
    u16 security_config_type;
    AcceptPolicy accept_policy;
    u8 has_action_frame;
    u16 padding2;
    s8 max_participants;
    s8 num_participants;
    std::array<NodeInfo, 8> nodes;
    u16 reserved3;
    u16 advertise_data_size;
    std::array<u8, 0x180> advertise_data;
    std::array<u8, 0x8c> reserved;
    u64 random_authentication_id;
};
static_assert(sizeof(NetworkInfo) == 0x480);
static_assert(std::has_unique_object_representations_v<NetworkInfo>);

struct SecurityParameter {
    std::array<u8, 16> data;
    std::array<u8, 16> session_id;
};
static_assert(sizeof(SecurityParameter) == 0x20);
static_assert(std::has_unique_object_representations_v<SecurityParameter>);

struct ScanFilter {
    s64 in_out_local_communication_id;
    u16 padding1;
    u16 filter;
    u32 padding2;
    std::array<u8, 16> network_id;
    u32 network_mode;
    std::array<u8, 6> mac_address;
    u8 ssid_length;
    std::array<u8, 33> ssid_sz;
    std::array<u8, 16> zeroes;
    u32 flags;
};
static_assert(sizeof(ScanFilter) == 0x60);
static_assert(std::has_unique_object_representations_v<ScanFilter>);

struct SecurityConfig {
    u16 type;
    u16 data_size;
    std::array<u8, 0x40> data;
};
static_assert(sizeof(SecurityConfig) == 0x44);
static_assert(std::has_unique_object_representations_v<SecurityConfig>);

struct UserConfig {
    std::array<u8, 32> nickname;
    std::array<u8, 16> zeroes;
};
static_assert(sizeof(UserConfig) == 0x30);
static_assert(std::has_unique_object_representations_v<UserConfig>);

struct NetworkConfig {
    IntentId intent_id;
    s16 channel;
    s8 max_participants;
    u8 padding3;
    s16 local_communication_version;
    std::array<u8, 10> padding4;
};
static_assert(sizeof(NetworkConfig) == 0x20);
static_assert(std::has_unique_object_representations_v<NetworkConfig>);

struct NodeLatestUpdate {
    u8 state_change;
    std::array<u8, 7> unknown;
};
static_assert(sizeof(NodeLatestUpdate) == 8);
static_assert(std::has_unique_object_representations_v<NodeLatestUpdate>);

constexpr size_t MAX_NUM_NODES = 8;
constexpr size_t MAX_NUM_STATIONS = MAX_NUM_NODES - 1;

constexpr u32 SCAN_FILTER_FLAG_LOCAL_COMMUNICATION_ID = 1U << 0;
constexpr u32 SCAN_FILTER_FLAG_NETWORK_ID = 1U << 1;
constexpr u32 SCAN_FILTER_FLAG_NETWORK_MODE = 1U << 2;
constexpr u32 SCAN_FILTER_FLAG_MAC_ADDRESS = 1U << 3;
constexpr u32 SCAN_FILTER_FLAG_SSID = 1U << 4;
constexpr u32 SCAN_FILTER_FLAG_FILTER = 1U << 5;

using IPv4Address = std::array<u8, 4>;
using Netmask = std::array<u8, 4>;

enum class NodeStatus {
    Disconnected,
    Connect,
    Connected,
};

class LanStation {
public:
    explicit LanStation(s8 node_id_);
    ~LanStation();

    void OverrideInfo(NodeInfo* node_info) const;

    s8 Id() const {
        return node_id;
    }

    NodeStatus Status() const {
        return status;
    }

    bool IsConnected() const {
        return status == NodeStatus::Connected;
    }

private:
    NodeStatus status = NodeStatus::Disconnected;
    s8 node_id = -1;

    Network::Socket socket;
};

class LanImpl {
public:
    bool Initialize();

    bool Finalize();

    State GetState() const;

    std::optional<NetworkInfo> GetNetworkInfo() const;

    std::optional<std::pair<IPv4Address, Netmask>> GetIpv4Address() const;

    std::optional<DisconnectReason> GetDisconnectReason() const;

    std::optional<SecurityParameter> GetSecurityParameter() const;

    std::optional<std::vector<NetworkInfo>> Scan(s16 channel, const ScanFilter& scan_filter);

    bool OpenAccessPoint();

    bool CloseAccessPoint();

    bool CreateNetwork(const SecurityConfig& security_config, const UserConfig& user_config,
                       const NetworkConfig& network_config);

    bool SetAdvertiseData(std::span<const u8> advertise_data);

    bool OpenStation();

    bool CloseStation();

    bool Connect();

    bool Disconnect();

private:
    static constexpr u16 LISTEN_PORTNO = 15543;

    void WorkerLoop();

    bool InitUDP();

    bool InitTCP();

    void HandlePollUDP(u16 revents);

    void HandlePollTCP(u16 revents);

    void InitNodeStateChange();

    void UpdateNodes();

    bool TestNodeStateChanged();

    void OnNetworkInfoChanged();

    State state = State::None;
    DisconnectReason disconnect_reason = DisconnectReason::None;
    NetworkInfo network_info;

    std::array<LanStation, MAX_NUM_STATIONS> stations{
        LanStation{1}, LanStation{2}, LanStation{3}, LanStation{4},
        LanStation{5}, LanStation{6}, LanStation{7},
    };
    std::array<NodeLatestUpdate, MAX_NUM_NODES> node_changes{};
    std::array<u8, MAX_NUM_NODES> node_last_states{};

    std::thread poll_thread;
    std::condition_variable poll_cv;
    std::mutex poll_mutex;
    std::atomic_bool stop{false};

    std::mt19937 random_engine{std::random_device{}()};

    Network::Socket udp;
    Network::Socket tcp;
};

} // namespace Service::LDN
