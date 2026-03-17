// Copyright 2020 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <condition_variable>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include "common/common_types.h"

namespace Core {

struct OnlineSubscriptionInfo {
    std::string display_subscription;
    std::string display_action;
    std::string url_action;
    bool enable_set_username;
    bool enable_set_profile;
    bool show_subscription_upgrade_notice;
};

void from_json(const nlohmann::json& json, OnlineSubscriptionInfo& info);

class OnlineInitiator {
public:
    explicit OnlineInitiator();
    ~OnlineInitiator();

    OnlineInitiator(const OnlineInitiator&) = delete;
    OnlineInitiator& operator=(const OnlineInitiator&) = delete;

    OnlineInitiator(OnlineInitiator&&) = delete;
    OnlineInitiator& operator=(OnlineInitiator&&) = delete;

    const OnlineSubscriptionInfo& GetSubscriptionInfo();

    void Connect();

    void Disconnect();

    void StartOnlineSession(u64 title_id);

    void EndOnlineSession();

    void WaitForCompletion() const;

    [[nodiscard]] bool IsConnected() const;

    [[nodiscard]] std::string ProfileApiUrl() const;

    [[nodiscard]] std::string FriendsApiUrl() const;

    [[nodiscard]] std::string TroubleshooterUrl() const;

    [[nodiscard]] std::string NotificationUrl() const;

    [[nodiscard]] std::string ConnectorUrl() const;

    [[nodiscard]] std::optional<std::string> RewriteUrl(const std::string& url) const;

    [[nodiscard]] std::string ResolveUrl(std::string dns, bool use_nsd) const;

    [[nodiscard]] u64 GetAccountId();

    [[nodiscard]] std::optional<std::string> LoadIdToken(u64 title_id) const;

    [[nodiscard]] std::optional<std::string> LoadIdTokenApp(std::string app_name) const;

private:
    void LoadAccountId();
    void LoadSubscriptionInfo();

    std::optional<std::string> LoadIdTokenInternal(
        std::vector<std::pair<std::string, std::string>> input_headers) const;

    void AskServer();

    std::string token;

    mutable std::recursive_mutex mutex;
    std::thread thread;

    std::mutex ask_mutex;
    std::condition_variable ask_condvar;

    std::unordered_map<std::string, std::string> url_rewrites;
    std::future<void> async_start_online_session;
    std::future<void> async_end_online_session;
    bool is_connected = false;
    u64 account_id{};
    OnlineSubscriptionInfo subscription_info{};
};

} // namespace Core
