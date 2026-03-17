// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <queue>

#include <nlohmann/json.hpp>

#include <QObject>

namespace Core {
class OnlineInitiator;
class System;
} // namespace Core

namespace ix {
class WebSocket;
}

class QMainWindow;
class QTimer;

class FriendsList;
class Notification;

class NotificationQueue : public QObject {
    Q_OBJECT

public:
    explicit NotificationQueue(Core::OnlineInitiator& online_initiator, FriendsList* friend_list,
                               QMainWindow* parent = nullptr);
    ~NotificationQueue();

signals:
    void ChangedAirplaneMode();

private slots:
    void OnTickNotifications();
    void OnChangedAirplaneMode();

private:
    enum class Priority {
        VeryLow = 0,
        Standard = 1,
        High = 2,
        Critical = 3,
    };

    enum class DisplayType {
        OutOfGame = 0,
        Banner = 1,
        Overlay = 2,
        Fullscreen = 3,
    };

    enum class NotificationType {
        General = 0,
        FriendRequest = 100,
        FriendRemoved = 101,
        FriendStatus = 102,
        GamePlannedMaintenance = 200,
    };

    struct NotificationEntry {
        QImage image;
        QString title;
        QString description;
        int priority = 0;

        constexpr bool operator<(const NotificationEntry& rhs) const noexcept {
            return priority < rhs.priority;
        }
    };

    void StartSocket();

    void ParseNotification(const std::string& input);
    void ParseFriendRequestAction(const nlohmann::json& json, Priority priority);
    void ParseFriendStatus(const nlohmann::json& json, Priority priority);
    void ParseGeneral(const nlohmann::json& json, Priority priority);

    void PushNotification(const QImage& image, const QString& title, const QString& description,
                          int priority);

    Core::System& system;
    Core::OnlineInitiator& online_initiator;
    FriendsList* friend_list;
    QMainWindow* parent;

    std::unique_ptr<ix::WebSocket> websocket;

    std::mutex notifications_mutex;
    std::priority_queue<NotificationEntry> notifications;
    std::future<void> async_start;

    QTimer* timer = nullptr;
    Notification* active_notification = nullptr;
};
