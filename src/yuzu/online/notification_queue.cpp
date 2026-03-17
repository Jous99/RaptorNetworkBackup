// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <nlohmann/json.hpp>

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <QImage>
#include <QMainWindow>
#include <QMenuBar>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <common/settings.h>

#include "common/logging/log.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/online_initiator.h"
#include "yuzu/online/friends.h"
#include "yuzu/online/notification.h"
#include "yuzu/online/notification_queue.h"
#include "yuzu/online/online_util.h"

using namespace std::chrono_literals;

namespace {

[[nodiscard]] std::optional<ix::WebSocketHttpHeaders> AuthorizationHeaders(
    Core::OnlineInitiator& online_initiator) {
    const std::optional id_token = online_initiator.LoadIdTokenApp("notification");
    if (!id_token) {
        return std::nullopt;
    }
    return ix::WebSocketHttpHeaders{
        {"R-Platform", "citrus"},
        {"Authorization", "Bearer " + id_token.value()},
    };
}

enum class FriendRequestAction {
    Incoming = 0,
    IncomingRetracted = 1,
    Rejected = 2,
    Accepted = 3,
};

[[nodiscard]] FriendRequestAction Convert(std::string_view input) {
    if (input == "0") {
        return FriendRequestAction::Incoming;
    } else if (input == "1") {
        return FriendRequestAction::IncomingRetracted;
    } else if (input == "2") {
        return FriendRequestAction::Rejected;
    } else if (input == "3") {
        return FriendRequestAction::Accepted;
    } else {
        throw std::runtime_error{fmt::format("Invalid friend request action: '{}'", input)};
    }
}

} // Anonymous namespace

NotificationQueue::NotificationQueue(Core::OnlineInitiator& online_initiator_,
                                     FriendsList* friend_list_, QMainWindow* parent_)
    : QObject(parent_), system{Core::System::GetInstance()}, online_initiator{online_initiator_},
      friend_list{friend_list_}, parent{parent_} {
    ix::initNetSystem();

    websocket = std::make_unique<ix::WebSocket>();
    websocket->setPingInterval(10);
    websocket->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        switch (msg->type) {
        case ix::WebSocketMessageType::Message:
            LOG_INFO(Frontend, "notification");
            ParseNotification(msg->str);
            friend_list->Refresh();
            break;
        case ix::WebSocketMessageType::Error:
            if (!online_initiator.IsConnected() || Settings::values.yuzu_token.empty()) {
                break;
            }
            break;
        default:
            break;
        }
    });

    if (!Settings::values.raptor_token.empty()) {
        async_start = std::async(&NotificationQueue::StartSocket, this);
    }

    connect(this, &NotificationQueue::ChangedAirplaneMode,
            &NotificationQueue::OnChangedAirplaneMode);

    timer = new QTimer(this);
    timer->setInterval(2s);
    connect(timer, &QTimer::timeout, this, [this] { OnTickNotifications(); });
    timer->start();
}

NotificationQueue::~NotificationQueue() = default;

void NotificationQueue::StartSocket() {
    websocket->setUrl(
        fmt::format("wss://{}/api/v1/notification", online_initiator.NotificationUrl()));
    websocket->setExtraHeaders([this] {
        return AuthorizationHeaders(online_initiator).value_or(ix::WebSocketHttpHeaders{});
    });
    websocket->start();
}

void NotificationQueue::OnChangedAirplaneMode() {
    if (async_start.valid()) {
        async_start.wait();
    }
    StartSocket();
}

void NotificationQueue::PushNotification(const QImage& image, const QString& title,
                                         const QString& description, int priority) {
    std::scoped_lock lock{notifications_mutex};
    notifications.push({
        .image = image,
        .title = title,
        .description = description,
        .priority = priority,
    });
}

void NotificationQueue::ParseNotification(const std::string& input) try {
    const nlohmann::json json = nlohmann::json::parse(input);
    const auto type = static_cast<NotificationType>(json.at("type").get<int>());
    const auto priority = static_cast<Priority>(json.at("priority").get<int>());
    [[maybe_unused]] const auto display_type =
        static_cast<DisplayType>(json.at("display_type").get<int>());
    const nlohmann::json properties = json.at("properties");
    switch (type) {
    case NotificationType::General:
        ParseGeneral(properties, priority);
        break;
    case NotificationType::FriendRequest:
        ParseFriendRequestAction(properties, priority);
        break;
    case NotificationType::FriendRemoved:
        break;
    case NotificationType::FriendStatus:
        ParseFriendStatus(properties, priority);
        break;
    case NotificationType::GamePlannedMaintenance:
        break;
    }

} catch (const std::exception& e) {
    LOG_ERROR(Frontend, "Error parsing notification: {}", e.what());
}

void NotificationQueue::ParseFriendRequestAction(const nlohmann::json& json, Priority priority) {
    const FriendRequestAction action = Convert(json.at("action").get<std::string>());
    const auto owner_avatar_url = json.at("owner_avatar_url").get<std::string>();
    const auto owner_name = json.at("owner_name").get<std::string>();
    const auto target_avatar_url = json.at("target_avatar_url").get<std::string>();
    const auto target_name = json.at("target_name").get<std::string>();

    switch (action) {
    case FriendRequestAction::Incoming: {
        PushNotification(DownloadImageUrl(AvatarUrl(owner_avatar_url, "64")),
                         tr("Incoming friend request"),
                         tr("%1 wants to be your friend").arg(QString::fromStdString(owner_name)),
                         static_cast<int>(priority));
        break;
    }
    case FriendRequestAction::IncomingRetracted:
        break;
    case FriendRequestAction::Accepted:
        PushNotification(DownloadImageUrl(AvatarUrl(target_avatar_url, "64")),
                         tr("Accepted friend request"),
                         tr("%1 is now your friend").arg(QString::fromStdString(target_name)),
                         static_cast<int>(priority));
        break;
    case FriendRequestAction::Rejected:
        break;
    }
}

void NotificationQueue::ParseFriendStatus(const nlohmann::json& json, Priority priority) {
    const auto player_avatar_url = json.at("player_avatar_url");
    const auto player_name = json.at("player_name");
    const auto status_code = json.at("status_code");
    const auto status_title_name = json.at("status_title_name");
    if (const bool is_online = status_code == "1"; is_online) {
        PushNotification(DownloadImageUrl(AvatarUrl(player_avatar_url, "64")),
                         QString::fromStdString(player_name),
                         QString::fromStdString(status_title_name), static_cast<int>(priority));
    }
}

void NotificationQueue::ParseGeneral(const nlohmann::json& json,
                                     NotificationQueue::Priority priority) {
    PushNotification(DownloadImageUrl(json.at("icon_url")),
                     QString::fromStdString(json.at("title")),
                     QString::fromStdString(json.at("description")), static_cast<int>(priority));
}

void NotificationQueue::OnTickNotifications() {
    if (active_notification) {
        if (active_notification->IsActive()) {
            return;
        } else {
            active_notification->deleteLater();
            active_notification = nullptr;
        }
    }

    std::unique_lock lock{notifications_mutex};
    if (notifications.empty()) {
        return;
    }
    [[maybe_unused]] const NotificationEntry entry = notifications.top();
    notifications.pop();
    lock.unlock();

    // Notifications can't be currently used on native widgets
    active_notification = new Notification(parent);
    active_notification->SetImage(entry.image);
    active_notification->SetTitle(entry.title);
    active_notification->SetDescription(entry.description);
    active_notification->SetHideRight(!system.IsPoweredOn());
    active_notification->Reposition(parent, parent->menuBar());
    active_notification->Play();
    active_notification->show();
}
