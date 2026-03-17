// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>

struct Friend {
    std::string account_id;
    std::string friend_code;
    std::string username;
    std::string avatar_url;
    std::string status_title_name;
    bool status_code = false;
};

struct FriendRequest {
    enum class Status {
        Normal,
        RetractedBySender,
        RejectedByRecipient,
    };

    std::string request_id;
    std::string sender_username;
    std::string sender_friend_code;
    std::string sender_avatar_url;
    std::string receiver_username;
    std::string receiver_friend_code;
    std::string receiver_avatar_url;
    Status status = Status::Normal;
};

struct BlockedUser {
    std::string account_id;
    std::string friend_code;
    std::string username;
    std::string avatar_url;
};

struct GeneralNotification {
    std::string title;
    std::string description;
    std::string icon_url;
};

struct FriendRequestNotification {
    std::string owner_name;
    std::string target_name;
    std::string owner_avatar_url;
    std::string target_avatar_url;
    std::string action;
};

struct FriendRemoved {
    std::string owner_name;
    std::string target_name;
    std::string owner_avatar_url;
    std::string target_avatar_url;
};

struct FriendStatusNotification {
    std::string player_name;
    std::string player_avatar_url;
    std::string status_code;
    std::string status_title_name;
};

struct GamePlannedMaintenanceNotification {
    std::string game_name;
    long long start_time;
    long long end_time;
};
