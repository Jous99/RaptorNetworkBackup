// Copyright 2020 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <fmt/format.h>

#include <nlohmann/json.hpp>

#include <common/hardware_id.h>
#include <common/settings.h>
#include <httplib.h>

#include "common/logging/log.h"
#include "core/online_initiator.h"

namespace Core {

void from_json(const nlohmann::json& json, OnlineSubscriptionInfo& info) {
    json.at("display_subscription").get_to(info.display_subscription);
    json.at("display_action").get_to(info.display_action);
    json.at("url_action").get_to(info.url_action);
    json.at("enable_set_username").get_to(info.enable_set_username);
    json.at("enable_set_profile").get_to(info.enable_set_profile);
    json.at("show_subscription_upgrade_notice").get_to(info.show_subscription_upgrade_notice);
}

constexpr const char ACCOUNTS_URL[] = "accounts-api-lp1.raptor.network";
constexpr const char CONFIG_URL[] = "config-lp1.raptor.network";

OnlineInitiator::OnlineInitiator() {
    Connect();
}

OnlineInitiator::~OnlineInitiator() {
    std::scoped_lock lock{mutex};
    if (thread.joinable()) {
        thread.join();
    }
}

void OnlineInitiator::Connect() {
    {
        std::scoped_lock lock{mutex};
        if (is_connected) {
            return;
        }
        if (thread.joinable()) {
            thread.join();
        }
        thread = std::thread(&OnlineInitiator::AskServer, this);
    }

    std::unique_lock lock{ask_mutex};
    ask_condvar.wait(lock);
}

void OnlineInitiator::Disconnect() {
    std::scoped_lock lock{mutex};
    is_connected = false;
}

void OnlineInitiator::StartOnlineSession(u64 title_id) {}

void OnlineInitiator::EndOnlineSession() {}

void OnlineInitiator::WaitForCompletion() const {
    std::scoped_lock lock{mutex};
}

bool OnlineInitiator::IsConnected() const {
    std::scoped_lock lock{mutex};
    return is_connected;
}

std::string OnlineInitiator::ProfileApiUrl() const {
    return "profile-lp1.raptor.network";
}

std::string OnlineInitiator::FriendsApiUrl() const {
    return "friends-lp1.raptor.network";
}

std::string OnlineInitiator::TroubleshooterUrl() const {
    return "status-lp1.raptor.network";
}

std::string OnlineInitiator::NotificationUrl() const {
    return "notification-lp1.raptor.network";
}

std::string OnlineInitiator::ConnectorUrl() const {
    return "connector-lp1.raptor.network";
}

std::optional<std::string> OnlineInitiator::RewriteUrl(const std::string& url) const {
    std::scoped_lock lock{mutex};
    const auto it = url_rewrites.find(url);
    if (it == url_rewrites.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::string OnlineInitiator::ResolveUrl(std::string dns, bool use_nsd) const {
    if (auto rewrite = RewriteUrl(dns)) {
        LOG_INFO(Core, "Rewrite '{}' to '{}'", dns, *rewrite);
        dns = std::move(*rewrite);
        return dns;
    }
    if (dns.find("nintendo") != std::string::npos || dns.find('%') != std::string::npos) {
        // Avoid connecting to Nintendo servers
        LOG_INFO(Core, "Trying to connect to Nintendo's server or environment server '{}'", dns);
        dns = "127.0.0.1";
    }
    return dns;
}

std::optional<std::string> OnlineInitiator::LoadIdTokenInternal(
    std::vector<std::pair<std::string, std::string>> input_headers) const {
    std::scoped_lock lock{mutex};
    //    if (!is_connected) {
    //        LOG_ERROR(Network, "Trying to load id token pair when online is not connected");
    //        return std::nullopt;
    //    }

    httplib::Headers headers;
    for (const auto& [key, value] : input_headers) {
        headers.emplace(key, value);
    }
    headers.emplace("Authorization", fmt::format("Bearer {}", Settings::values.raptor_token));
    headers.emplace("R-ClientId", "citrus");
    headers.emplace("R-HardwareId", Common::GetRaptorHardwareID());

    httplib::SSLClient client(ACCOUNTS_URL);
    const auto response = client.Post("/api/v1/client/login", headers, "", "");
    if (!response) {
        LOG_ERROR(Network, "Failed to request online token from server");
        return std::nullopt;
    }

    const auto iter = response->headers.find("R-ClearClientToken");
    if (iter != response->headers.end() && iter->second == "1") {
        Settings::values.raptor_token.clear();
    }

    switch (response->status) {
    case 200:
        break;
    case 400:
        LOG_ERROR(Network, "Game has no online functionality");
        return std::nullopt;
    case 401:
        LOG_ERROR(Network, "Missing token in headers");
        return std::nullopt;
    case 403:
        LOG_ERROR(Network, "User not allowed online");
        return std::nullopt;
    default:
        LOG_ERROR(Network, "Network error={}", response->status);
        return std::nullopt;
    }

    return response->body;
}

void OnlineInitiator::AskServer() try {
    std::scoped_lock lock{mutex};
    {
        std::scoped_lock ask_lock{ask_mutex};
        ask_condvar.notify_one();
    }

    if (Settings::values.raptor_token.empty()) {
        return;
    }

    LoadAccountId();

    url_rewrites.clear();

    const auto config = LoadIdTokenApp("config");

    httplib::Headers headers{
        {"Authorization", fmt::format("Bearer {}", config.value())},
    };

    httplib::SSLClient client(CONFIG_URL);
    const auto response = client.Get("/api/v1/rewrites", headers);
    if (!response || response->status != 200) {
        return;
    }

    const auto json = nlohmann::json::parse(response->body);
    for (const auto& entry : json) {
        url_rewrites.insert_or_assign(entry.at("source").get<std::string>(),
                                      entry.at("destination").get<std::string>());
    }

    is_connected = account_id != 0;
} catch (const std::exception& e) {
    url_rewrites.clear();
    LOG_ERROR(Core, "{}", e.what());
}

std::optional<std::string> OnlineInitiator::LoadIdTokenApp(std::string app_name) const {
    return LoadIdTokenInternal({{"R-Target", std::move(app_name)}});
}

std::optional<std::string> OnlineInitiator::LoadIdToken(u64 title_id) const {
    return LoadIdTokenInternal({{"R-TitleId", fmt::format("{:X}", title_id)}});
}

void OnlineInitiator::LoadAccountId() {
    std::scoped_lock lock{mutex};
    if (account_id != 0) {
        return;
    }

    //    if (!is_connected) {
    //        LOG_ERROR(Network, "Trying to load id token pair when online is not connected");
    //        return;
    //    }

    httplib::Headers headers;
    headers.emplace("Authorization", fmt::format("Bearer {}", Settings::values.raptor_token));
    headers.emplace("R-ClientId", "citrus");
    headers.emplace("R-HardwareId", Common::GetRaptorHardwareID());

    httplib::SSLClient client(ACCOUNTS_URL);
    const auto response = client.Get("/api/v1/client/account_id", headers);
    if (!response || response->status != 200) {
        if (response) {
            const auto iter = response->headers.find("R-ClearClientToken");
            if (iter != response->headers.end() && iter->second == "1") {
                Settings::values.raptor_token.clear();
                Connect();
            }
        }

        LOG_ERROR(Network, "Failed to request client account id from server, {}");
        return;
    }

    try {
        account_id = std::stoull(response->body, nullptr, 0x10);
    } catch (...) {
    }
}

u64 OnlineInitiator::GetAccountId() {
    LoadAccountId();
    return account_id;
}

void OnlineInitiator::LoadSubscriptionInfo() {
    std::scoped_lock lock{mutex};

    httplib::Headers headers;
    headers.emplace("Authorization", fmt::format("Bearer {}", Settings::values.raptor_token));
    headers.emplace("R-ClientId", "citrus");
    headers.emplace("R-HardwareId", Common::GetRaptorHardwareID());

    httplib::SSLClient client(ACCOUNTS_URL);
    const auto response = client.Get("/api/v1/client/subscription", headers);
    if (!response || response->status != 200) {
        LOG_ERROR(Network, "Failed to request client subscription info from server");
        return;
    }

    try {
        subscription_info = nlohmann::json::parse(response->body);
    } catch (...) {
    }
}

const OnlineSubscriptionInfo& OnlineInitiator::GetSubscriptionInfo() {
    LoadSubscriptionInfo();
    return subscription_info;
}

} // namespace Core
