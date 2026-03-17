// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <memory>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <fmt/format.h>

#include <QDesktopServices>
#include <QMainWindow>
#include <QPushButton>
#include <QString>
#include <QThread>
#include <QUrl>
#include <common/hardware_id.h>

#include "common/scm_rev.h"
#include "common/settings.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/hle/kernel/process.h"
#include "core/online_initiator.h"
#include "yuzu/online/monitor.h"

namespace {

using namespace std::chrono_literals;

constexpr OnlineStatus AIRPLANE_MODE{
    .text = QT_TR_NOOP("Airplane Mode"),
    .tooltip = QT_TR_NOOP("Online functionality is disabled.\nClick to enable."),
    .icon = "airplanemode",
};
constexpr OnlineStatus CONNECTING_INITIATOR{
    .text = QT_TR_NOOP("Retrieving Configuration"),
    .tooltip = QT_TR_NOOP("yuzu is connecting to the server.\nPlease wait a moment."),
    .icon = "sync",
};
constexpr OnlineStatus CONNECTING_MAIN_SERVER{
    .text = QT_TR_NOOP("Connecting to Main Server"),
    .tooltip = QT_TR_NOOP("yuzu is connecting to the server.\nPlease wait a moment."),
    .icon = "sync",
};
constexpr OnlineStatus CONNECTING_USER_STATUS{
    .text = QT_TR_NOOP("Verifying User Status"),
    .tooltip = QT_TR_NOOP("yuzu is validating your user account.\nPlease wait a moment."),
    .icon = "sync",
};
constexpr OnlineStatus CONNECTING_GAME_SERVER{
    .text = QT_TR_NOOP("Connecting to Game Server"),
    .tooltip = QT_TR_NOOP("yuzu is connecting to the game server.\nPlease wait a moment."),
    .icon = "sync",
};
constexpr OnlineStatus CONNECTING_NEW_TOKEN{
    .text = QT_TR_NOOP("Connecting to Accounts Server"),
    .tooltip = QT_TR_NOOP("yuzu is connecting to the server.\nPlease wait a moment."),
    .icon = "sync",
    .retry_time = 1s,
};
constexpr OnlineStatus DISCONNECTED{
    .text = QT_TR_NOOP("Disconnected"),
    .tooltip = QT_TR_NOOP("yuzu could not connect to the server"),
    .icon = "public_off",
    .retry_time = 10s,
};
constexpr OnlineStatus NOT_LOGGED_IN{
    .text = QT_TR_NOOP("Click Here to Login to Raptor Network"),
    .tooltip = QT_TR_NOOP("You must be logged into Raptor Network to play games online."),
    .icon = "public_off",
    .retry_time = 0s,
};
constexpr OnlineStatus AUTHORIZING{
    .text = QT_TR_NOOP("Waiting for Login to Complete"),
    .tooltip = QT_TR_NOOP("Please complete the required steps in the browser."),
    .icon = "sync",
    .retry_time = 1s,
};
constexpr OnlineStatus NO_TOKEN_PROVIDED{
    .text = QT_TR_NOOP("No Token Provided"),
    .tooltip = QT_TR_NOOP("Go to Emulation > Configure > General > Web to provide a token."),
    .icon = "public_off",
    .retry_time = 0s,
};
constexpr OnlineStatus TEMPORARY_BAN{
    .text = QT_TR_NOOP("Temporarily Banned"),
    .tooltip = QT_TR_NOOP("User has been temporarily banned"),
    .icon = "public_off",
    .retry_time = 30s,
    .is_connected = true,
};
constexpr OnlineStatus PERMANENT_BAN{
    .text = QT_TR_NOOP("Permanently Banned"),
    .tooltip = QT_TR_NOOP("User has been permanently banned"),
    .icon = "public_off",
    .retry_time = 30s,
    .is_connected = true,
};
constexpr OnlineStatus UNKNOWN_ERROR{
    .text = QT_TR_NOOP("Unknown Connection Error"),
    .tooltip = QT_TR_NOOP("Unknown error"),
    .icon = "public_off",
    .retry_time = 60s,
    .is_connected = true,
};
constexpr OnlineStatus CONNECTED{
    .text = QT_TR_NOOP("Connected"),
    .tooltip = QT_TR_NOOP("Successfully Connected"),
    .icon = "public",
    .retry_time = 30s,
    .is_connected = true,
    .is_successful = true,
    .continue_connection = true,
};
constexpr OnlineStatus PLANNED_MAINTENANCE{
    .text = QT_TR_NOOP("Planned Maintenance"),
    .tooltip = QT_TR_NOOP("Server is under a planned maintenance session."),
    .icon = "public",
    .retry_time = 30s,
    .is_connected = true,
    .continue_connection = false,
};
constexpr OnlineStatus SERVER_DOWN{
    .text = QT_TR_NOOP("Server Unreachable"),
    .tooltip = QT_TR_NOOP("The server is connected but unable to serve requests."),
    .icon = "public_off",
    .retry_time = 30s,
    .is_connected = true,
    .continue_connection = false,
};
constexpr OnlineStatus DEGRADED_PERFORMANCE{
    .text = QT_TR_NOOP("Degraded Performance"),
    .tooltip = QT_TR_NOOP("The server is currently experiencing degraded performance"),
    .icon = "public",
    .retry_time = 30s,
    .is_connected = true,
    .continue_connection = true,
};
constexpr OnlineStatus TAKEN_OFFLINE{
    .text = QT_TR_NOOP("Taken Offline"),
    .tooltip = QT_TR_NOOP("The server has been taken offline."),
    .icon = "public_off",
    .retry_time = 30s,
    .is_connected = true,
};
constexpr OnlineStatus PARTIAL_MAINTENANCE{
    .text = QT_TR_NOOP("Partial Maintenance"),
    .tooltip = QT_TR_NOOP("Server is under a partial planned maintenance session"),
    .icon = "public",
    .retry_time = 30s,
    .is_connected = true,
    .continue_connection = false,
};
constexpr OnlineStatus PARTIAL_INCIDENT{
    .text = QT_TR_NOOP("Partial Incident"),
    .tooltip = QT_TR_NOOP("Parts of the server are down"),
    .icon = "public",
    .retry_time = 30s,
    .is_connected = true,
    .continue_connection = false,
};
constexpr OnlineStatus PARTIAL_DEGRADED_PERFORMANCE{
    .text = QT_TR_NOOP("Partial Degraded Performance"),
    .tooltip = QT_TR_NOOP("Parts of the server are running slower than normal"),
    .icon = "public",
    .retry_time = 30s,
    .is_connected = true,
    .continue_connection = false,
};
constexpr OnlineStatus PARTIAL_OFFLINE{
    .text = QT_TR_NOOP("Taken Down Partially"),
    .tooltip = QT_TR_NOOP("Parts of the server are taken down"),
    .icon = "public",
    .retry_time = 30s,
    .is_connected = true,
    .continue_connection = false,
};
constexpr OnlineStatus GAME_NO_ONLINE{
    .text = QT_TR_NOOP("Game Not Supported By Raptor"),
    .tooltip = QT_TR_NOOP("Connected but the game has no online functionality"),
    .icon = "public_off",
    .retry_time = 30s,
    .is_connected = true,
};
constexpr OnlineStatus NO_MEMBERSHIP{
    .text = QT_TR_NOOP("No Membership"),
    .tooltip = QT_TR_NOOP("Account has no online membership"),
    .icon = "public_off",
    .retry_time = 30s,
    .is_connected = true,
};
constexpr OnlineStatus LICENSE_NOT_ACCEPTED{
    .text = QT_TR_NOOP("Terms Not Accepted"),
    .tooltip = QT_TR_NOOP("The Raptor Terms have not been accepted yet"),
    .icon = "public_off",
    .retry_time = 30s,
    .is_connected = true,
};
constexpr OnlineStatus INVALID_TOKEN{
    .text = QT_TR_NOOP("Token Error"),
    .tooltip = QT_TR_NOOP("An error occurred during user verification"),
    .icon = "public_off",
    .retry_time = 60s,
    .is_connected = true,
    .continue_connection = true,
};
constexpr OnlineStatus NOT_REGISTERED{
    .text = QT_TR_NOOP("Connected"),
    .tooltip = QT_TR_NOOP("Successfully connected with no online account activity"),
    .icon = "public",
    .retry_time = 30s,
    .is_connected = true,
    .is_successful = true,
    .continue_connection = true,
};
constexpr OnlineStatus CLIENT_NOT_SUPPORTED{
    .text = QT_TR_NOOP("Client Not Supported"),
    .tooltip = QT_TR_NOOP("The client you are using is not supported for online. Please use an "
                          "official yuzu Early Access build."),
    .icon = "public_off",
    .retry_time = 120s,
    .is_connected = true,
};
constexpr OnlineStatus CLIENT_OUTDATED{
    .text = QT_TR_NOOP("Client Outdated"),
    .tooltip = QT_TR_NOOP("Your version of yuzu is too old to connect to the network. Please run "
                          "the installer to update to the latest version."),
    .icon = "public_off",
    .retry_time = 120s,
    .is_connected = true,
};

constexpr std::array<std::pair<int, OnlineStatus>, 16> STATUS_TABLE{{
    {0, CONNECTED},
    {1, PLANNED_MAINTENANCE},
    {2, SERVER_DOWN},
    {3, DEGRADED_PERFORMANCE},
    {4, TAKEN_OFFLINE},
    {5, PARTIAL_MAINTENANCE},
    {6, PARTIAL_INCIDENT},
    {7, PARTIAL_DEGRADED_PERFORMANCE},
    {8, PARTIAL_OFFLINE},
    {10, GAME_NO_ONLINE},
    {11, CLIENT_NOT_SUPPORTED},
    {12, CLIENT_OUTDATED},
    {50, NO_MEMBERSHIP},
    {51, LICENSE_NOT_ACCEPTED},
    {52, INVALID_TOKEN},
    {53, NOT_REGISTERED},
}};

const OnlineStatus* FindStatus(int code) {
    if (code >= 100 && code <= 199) {
        return &TEMPORARY_BAN;
    }
    if (code >= 200 && code <= 299) {
        return &PERMANENT_BAN;
    }

    const auto it =
        std::ranges::find_if(STATUS_TABLE, [code](const auto& pair) { return pair.first == code; });
    return it == STATUS_TABLE.end() ? &UNKNOWN_ERROR : &it->second;
}

} // Anonymous namespace

OnlineStatusMonitor::OnlineStatusMonitor(Core::OnlineInitiator& online_initiator_)
    : online_initiator(online_initiator_) {
    setObjectName(QStringLiteral("OnlineStatusButton"));
    setFocusPolicy(Qt::NoFocus);
    setCheckable(true);
    setLayoutDirection(Qt::RightToLeft);

    if (Settings::values.raptor_token.empty()) {
        setChecked(false);
        OnOnlineStatusChanged(&NOT_LOGGED_IN);
    } else {
        setChecked(true);
        OnOnlineStatusChanged(&DISCONNECTED);
    }

    connect(this, &QPushButton::clicked, [this] {
        if (Settings::values.raptor_token.empty()) {
            setChecked(true);
            worker->StartLoginFlow();
        }
    });

    worker = new MonitorWorker(online_initiator, this);
    connect(worker, &MonitorWorker::OnlineStatusChanged, this,
            &OnlineStatusMonitor::OnOnlineStatusChanged);
    connect(worker, &MonitorWorker::ChangeStatus, this, &OnlineStatusMonitor::OnChangeStatus);
    connect(worker, &MonitorWorker::AcceptInput, this, &OnlineStatusMonitor::OnAcceptInput);
    connect(worker, &MonitorWorker::RejectInput, this, &OnlineStatusMonitor::OnRejectInput);
    connect(worker, &MonitorWorker::SaveConfig, this, &OnlineStatusMonitor::OnSaveConfig);
    connect(this, &OnlineStatusMonitor::OnStartLoginExternal, this,
            &OnlineStatusMonitor::StartLoginExternal);
    worker->start();
}

OnlineStatusMonitor::~OnlineStatusMonitor() {
    worker->Quit();
    worker->wait();
}

void OnlineStatusMonitor::OnSaveConfig() {
    emit SaveConfig();
}

void OnlineStatusMonitor::Refresh() {
    worker->Refresh();
}

void OnlineStatusMonitor::DisableAirplaneMode() {
    online_initiator.Connect();
    emit ChangeAirplaneMode();
}

void OnlineStatusMonitor::OnChangeAirplaneMode() {
    setChecked(true);
    worker->Refresh();
}

void OnlineStatusMonitor::OnOnlineStatusChanged(const OnlineStatus* status) {
    OnChangeStatus(status);
}

void OnlineStatusMonitor::OnChangeStatus(const OnlineStatus* status) {
    setText(tr(status->text));
    setToolTip(tr(status->tooltip));

    QPixmap pixmap = QIcon::fromTheme(QString::fromUtf8(status->icon)).pixmap(24);
    QIcon icon;
    icon.addPixmap(pixmap, QIcon::Mode::Normal);
    icon.addPixmap(pixmap, QIcon::Mode::Disabled);
    setIcon(icon);
}

void OnlineStatusMonitor::OnAcceptInput() {
    setEnabled(true);
}

void OnlineStatusMonitor::OnRejectInput() {
    setEnabled(false);
}

bool OnlineStatusMonitor::IsAirplaneMode() const noexcept {
    return !isChecked();
}

void OnlineStatusMonitor::OnStartLoginExternal() {
    setChecked(true);
    worker->StartLoginFlow();
}

void MonitorWorker::StartLoginFlow() {
    is_awaiting_authorization = true;
    Refresh();
}

MonitorWorker::MonitorWorker(Core::OnlineInitiator& online_initiator_, QObject* parent)
    : QThread(parent), system(Core::System::GetInstance()), online_initiator(online_initiator_) {}

MonitorWorker::~MonitorWorker() = default;

void MonitorWorker::Refresh() {
    event.Set();
}

void MonitorWorker::Quit() {
    shutdown = true;
    event.Set();
}

void MonitorWorker::run() {
    while (!shutdown) {
        WorkerLoop();
    }
}

void MonitorWorker::WorkerLoop() {
    event.Reset();

    UpdateInitiator();

    if (status->continue_connection) {
        UpdateMainServer();
    }
    if (status->continue_connection) {
        UpdateUserStatus();
    }
    if (status->continue_connection && status->is_successful) {
        UpdateGameServer();
    }

    if (is_awaiting_authorization) {
        if (redirect_token.empty()) {
            httplib::SSLClient client("accounts-api-lp1.raptor.network");
            const auto response =
                client.Post("/api/v1/client/register/redirect",
                            httplib::Headers{{"R-HardwareId", Common::GetRaptorHardwareID()},
                                             {"R-ClientId", "citrus"}},
                            "", "");
            if (response && response->status == 200) {
                try {
                    const auto json = nlohmann::json::parse(response->body);
                    const auto url = json.at("url").get<std::string>();
                    redirect_token = json.at("token").get<std::string>();
                    QDesktopServices::openUrl(QUrl(QString::fromStdString(url)));
                    status = &AUTHORIZING;
                } catch (...) {
                    redirect_token.clear();
                }
            }
        } else {
            httplib::SSLClient client("accounts-api-lp1.raptor.network");
            const auto response =
                client.Get("/api/v1/client/register/redirect/token", {{"R-Token", redirect_token}});
            if (response) {
                if (response->status == 200) {
                    Settings::values.raptor_token = response->body;
                    emit SaveConfig();
                    status = &CONNECTING_NEW_TOKEN;
                    redirect_token.clear();
                    is_awaiting_authorization = false;
                    online_initiator.Connect();
                } else if (response->status == 204) {
                    status = &AUTHORIZING;
                } else {
                    status = &NOT_LOGGED_IN;
                    redirect_token.clear();
                    is_awaiting_authorization = false;
                }
            }
        }
    }

    emit OnlineStatusChanged(status);
    emit AcceptInput();

    if (status->retry_time == 0s) {
        event.Wait();
    } else {
        event.WaitFor(status->retry_time);
    }
}

void MonitorWorker::UpdateOffline() {
    online_initiator.Disconnect();
    is_main_connected = false;
    is_user_connected = false;
    is_game_connected = false;

    status = &AIRPLANE_MODE;
    emit OnlineStatusChanged(status);
}

void MonitorWorker::UpdateInitiator() {
    if (!online_initiator.IsConnected() && status != &AUTHORIZING) {
        emit OnlineStatusChanged(&CONNECTING_INITIATOR);
    }

    if (!client) {
        const std::string url = online_initiator.TroubleshooterUrl();
        client = std::make_unique<httplib::SSLClient>(url);
        client->set_follow_location(true);
    }

    online_initiator.Connect();
    if (!online_initiator.IsConnected()) {
        status = Settings::values.raptor_token.empty() ? &NOT_LOGGED_IN : &INVALID_TOKEN;
        return;
    }

    status = &CONNECTED;
}

void MonitorWorker::UpdateMainServer() {
    if (!is_main_connected) {
        emit OnlineStatusChanged(&CONNECTING_MAIN_SERVER);
    }
    is_main_connected = false;

    status = ProcessMainResponse(client->Get("/api/v1/status/general"));
    if (status->continue_connection) {
        httplib::Headers client_version{
            {"R-Client-Name", "citrus"},
            {"R-Client-Version", Common::g_build_version},
        };
        status = ProcessMainResponse(client->Get("/api/v1/status/client", client_version));
    }

    is_main_connected = status->is_connected;
}

void MonitorWorker::UpdateUserStatus() {
    if (!is_user_connected) {
        emit OnlineStatusChanged(&CONNECTING_USER_STATUS);
    }
    is_user_connected = false;

    if (Settings::values.raptor_token.empty()) {
        status = &NOT_LOGGED_IN;
        return;
    }

    httplib::Headers headers;
    headers.emplace("Authorization", fmt::format("Bearer {}", Settings::values.raptor_token));
    headers.emplace("R-ClientId", "citrus");
    headers.emplace("R-HardwareId", Common::GetRaptorHardwareID());
    const auto response = client->Get("/api/v1/status/token/standard", headers);
    if (!response) {
        status = &DISCONNECTED;
        return;
    }

    is_user_connected = true;

    const OnlineStatus* const user_status = ProcessBody(response->body);
    if (user_status != &CONNECTED) {
        status = user_status;
    }
}

void MonitorWorker::UpdateGameServer() {
    if (!system.IsPoweredOn()) {
        return;
    }
    const Kernel::Process* const current_process = system.CurrentProcess();
    if (!current_process) {
        return;
    }
    const u64 title_id = current_process->GetTitleID();

    if (!is_game_connected) {
        emit OnlineStatusChanged(&CONNECTING_GAME_SERVER);
    }

    const std::string url = fmt::format("/api/v1/status/title/{:X}", title_id);
    const OnlineStatus* const game_status = ProcessGameResponse(client->Get(url.c_str()));
    is_game_connected = game_status->is_connected;
    if (!game_status->is_successful) {
        status = game_status;
    }
}

const OnlineStatus* MonitorWorker::ProcessMainResponse(const httplib::Result& response) {
    if (!response) {
        return &DISCONNECTED;
    }
    if (response->status != 200) {
        return &UNKNOWN_ERROR;
    }
    return ProcessBody(response->body);
}

const OnlineStatus* MonitorWorker::ProcessGameResponse(const httplib::Result& response) {
    if (response && response->status == 400) {
        return &GAME_NO_ONLINE;
    }
    return ProcessMainResponse(response);
}

const OnlineStatus* MonitorWorker::ProcessBody(std::string_view body) {
    int status;
    if (std::from_chars(body.data(), body.data() + body.size(), status).ec != std::errc{}) {
        return &UNKNOWN_ERROR;
    }
    return FindStatus(status);
}

void MonitorWorker::StartLoginExternal() {
    StartLoginFlow();
}
