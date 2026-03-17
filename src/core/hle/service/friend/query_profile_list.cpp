// Copyright 2020 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <charconv>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

#include <httplib.h>

#include <nlohmann/json.hpp>

#include "common/logging/log.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/hle_ipc.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_writable_event.h"
#include "core/hle/result.h"
#include "core/hle/service/friend/query_profile_list.h"
#include "core/online_initiator.h"

namespace Service::Friend {

namespace {

[[nodiscard]] std::string BuildBody(const std::vector<u64>& account_id_list) {
    std::string body;
    body.reserve(17 * account_id_list.size());
    for (const u64 account_id : account_id_list) {
        body += fmt::format("{:016x}&", account_id);
    }
    // Remove the last '&'
    body.pop_back();
    return body;
}

[[nodiscard]] std::optional<ProfileList> GetProfileList(
    const Core::OnlineInitiator& online_initiator, u64 title_id,
    const std::vector<u64>& account_id_list) try {
    const std::optional id_token = online_initiator.LoadIdTokenApp("profile");
    if (!id_token) {
        return std::nullopt;
    }

    const httplib::Request request{
        .method = "GET",
        .path = "/api/v1/list",
        .headers = {{"Authorization", "Bearer " + id_token.value()}},
        .body = BuildBody(account_id_list),
    };

    httplib::SSLClient client(online_initiator.ProfileApiUrl());
    httplib::Response response;
    if (!client.send(request, response)) {
        LOG_ERROR(Service, "No response from server");
        return std::nullopt;
    }
    if (response.status != 200) {
        LOG_ERROR(Service, "Error from server: {}", response.status);
        return std::nullopt;
    }

    ProfileList profile_list;
    profile_list.reserve(account_id_list.size());

    for (const auto& entry : nlohmann::json::parse(response.body)) {
        const std::string username = entry.at("username").get<std::string>();
        const std::string image_url = entry.at("avatar_url").get<std::string>();
        const std::string pid = entry.at("pid").get<std::string>();
        u64 account_id;
        if (std::from_chars(pid.data(), pid.data() + pid.size(), account_id, 16).ec !=
            std::errc{}) {
            LOG_ERROR(Service, "Error parsing account id");
            return std::nullopt;
        }

        Profile& profile = profile_list.emplace_back(Profile{
            .account_id = account_id,
            .is_valid_profile = 1,
        });

        std::copy_n(username.data(), std::min(username.size(), profile.name.size()),
                    profile.name.data());
        std::copy_n(image_url.data(), std::min(image_url.size(), profile.profile_image_url.size()),
                    profile.profile_image_url.data());
    }
    return std::make_optional(std::move(profile_list));

} catch (const std::exception& e) {
    LOG_ERROR(Service, "Error querying profile list: {}", e.what());
    return std::nullopt;
}

[[nodiscard, maybe_unused]] std::optional<std::vector<u64>> GetFriendList(
    const Core::OnlineInitiator& online_initiator) try {
    const std::optional id_token = online_initiator.LoadIdTokenApp("friends");
    if (!id_token) {
        return std::nullopt;
    }
    const httplib::Headers headers{
        {"Authorization", "Bearer " + id_token.value()},
    };
    httplib::SSLClient client(online_initiator.FriendsApiUrl());
    auto response = client.Get("/api/v1/me/friends", headers);
    if (!response || response->status != 200) {
        return std::nullopt;
    }
    const nlohmann::json json = nlohmann::json::parse(response->body);
    std::vector<u64> friends;
    friends.reserve(json.size());
    for (const auto& entry : json) {
        const std::string pid = entry.at("pid");
        u64 account_id;
        if (std::from_chars(pid.data(), pid.data() + pid.size(), account_id, 16).ec !=
            std::errc{}) {
            LOG_ERROR(Service, "Error parsing account id");
            return std::nullopt;
        }
        friends.push_back(account_id);
    }
    return friends;

} catch (const std::exception& e) {
    LOG_ERROR(Service, "Error querying friend list: {}", e.what());
    return std::nullopt;
}

[[nodiscard]] std::optional<std::vector<u64>> GetBlockedUsers(
    const Core::OnlineInitiator& online_initiator) try {
    const std::optional id_token = online_initiator.LoadIdTokenApp("friends");
    if (!id_token) {
        return std::nullopt;
    }
    const httplib::Headers headers{
        {"Authorization", "Bearer " + id_token.value()},
    };
    httplib::SSLClient client(online_initiator.FriendsApiUrl());
    auto response = client.Get("/api/v1/block", headers);
    if (!response || response->status != 200) {
        return std::nullopt;
    }
    const nlohmann::json json = nlohmann::json::parse(response->body);
    std::vector<u64> friends;
    friends.reserve(json.size());
    for (const auto& entry : json) {
        const std::string pid = entry.at("blocked_account_id");
        u64 account_id;
        if (std::from_chars(pid.data(), pid.data() + pid.size(), account_id, 16).ec !=
            std::errc{}) {
            LOG_ERROR(Service, "Error parsing account id");
            return std::nullopt;
        }
        friends.push_back(account_id);
    }
    return friends;

} catch (const std::exception& e) {
    LOG_ERROR(Service, "Error querying blocked users: {}", e.what());
    return std::nullopt;
}

} // Anonymous namespace

void GetProfileListWork::Execute(IFriendService*) {
    std::optional list = GetProfileList(*online_initiator, title_id, account_id_list);
    if (list) {
        profile_list = std::move(*list);
        result_code = RESULT_SUCCESS;
    } else {
        // TODO: Set a real error code
        result_code = RESULT_UNKNOWN;
    }
}

void GetProfileListWork::Response(Kernel::HLERequestContext& ctx) {
    if (!profile_list.empty()) {
        ctx.WriteBuffer(profile_list);
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result_code);
    event->GetWritableEvent()->Signal();
}

void GetFriendsListWork::Execute(IFriendService*) {
    account_ids = {0xBB00000100000002, 0xBB00000100000002, 0xBB00000100000002};
    result_code = RESULT_SUCCESS;
}

void GetFriendsListWork::Response(Kernel::HLERequestContext& ctx) {
    if (!account_ids.empty()) {
        ctx.WriteBuffer(account_ids);
    }
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result_code);
    rb.Push(static_cast<u32>(account_ids.size()));
    event->GetWritableEvent()->Signal();
}

void GetBlockedUsersWork::Execute(IFriendService*) {
    std::optional list = GetBlockedUsers(*online_initiator);
    if (list) {
        account_ids = std::move(*list);
        result_code = RESULT_SUCCESS;
    } else {
        // TODO: Set a real error code
        result_code = RESULT_UNKNOWN;
    }
}

void GetBlockedUsersWork::Response(Kernel::HLERequestContext& ctx) {
    if (!account_ids.empty()) {
        ctx.WriteBuffer(account_ids);
    }
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result_code);
    rb.Push(static_cast<u32>(account_ids.size()));
}

} // namespace Service::Friend
