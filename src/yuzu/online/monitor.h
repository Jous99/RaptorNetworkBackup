// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string_view>
#include <thread>

#include <QPushButton>
#include <QThread>

#include "common/thread.h"

namespace httplib {
class SSLClient;
class Result;
} // namespace httplib

namespace Core {
class OnlineInitiator;
class System;
} // namespace Core

namespace WebService {
class Client;
}

class QObject;

struct OnlineStatus {
    const char* text;
    const char* tooltip;
    const char* icon;
    std::chrono::seconds retry_time;
    bool is_connected;
    bool is_successful;
    bool continue_connection;
};

class MonitorWorker : public QThread {
    Q_OBJECT

public:
    explicit MonitorWorker(Core::OnlineInitiator& online_initiator, QObject* parent = nullptr);
    ~MonitorWorker() override;

    void StartLoginFlow();

    void Refresh();

    void Quit();

    void run() override;

signals:
    void OnlineStatusChanged(const OnlineStatus* status);

    void ChangeStatus(const OnlineStatus* status);

    void AcceptInput();

    void RejectInput();

    void SaveConfig();

public slots:
    void StartLoginExternal();

private:
    void WorkerLoop();

    void UpdateOffline();

    void UpdateInitiator();

    void UpdateMainServer();

    void UpdateUserStatus();

    void UpdateGameServer();

    [[nodiscard]] const OnlineStatus* ProcessMainResponse(const httplib::Result& result);

    [[nodiscard]] const OnlineStatus* ProcessGameResponse(const httplib::Result& result);

    [[nodiscard]] const OnlineStatus* ProcessBody(std::string_view body);

    Core::System& system;
    Core::OnlineInitiator& online_initiator;

    Common::Event event;

    std::unique_ptr<httplib::SSLClient> client;
    const OnlineStatus* status = nullptr;
    std::atomic_bool shutdown{false};
    std::string redirect_token;
    bool is_awaiting_authorization = false;
    bool is_main_connected = false;
    bool is_user_connected = false;
    bool is_game_connected = false;
};

class OnlineStatusMonitor : public QPushButton {
    Q_OBJECT

public:
    explicit OnlineStatusMonitor(Core::OnlineInitiator& online_initiator);
    ~OnlineStatusMonitor() override;

    void Refresh();

    void DisableAirplaneMode();

signals:
    void StartLoginExternal();

    void ChangeAirplaneMode();

    void SaveConfig();

public slots:
    void OnStartLoginExternal();

private slots:
    void OnSaveConfig();

    void OnChangeAirplaneMode();

    void OnOnlineStatusChanged(const OnlineStatus* status);

    void OnChangeStatus(const OnlineStatus* status);

    void OnAcceptInput();

    void OnRejectInput();

private:
    [[nodiscard]] bool IsAirplaneMode() const noexcept;

    Core::OnlineInitiator& online_initiator;

    MonitorWorker* worker = nullptr;
};
