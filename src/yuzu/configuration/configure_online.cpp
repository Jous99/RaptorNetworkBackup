// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include <QBuffer>
#include <QFileDialog>
#include <QGraphicsPixmapItem>
#include <QIcon>
#include <QInputDialog>
#include <QMessageBox>

#include <QtConcurrent/QtConcurrentRun>

#include <QDesktopServices>
#include <common/settings.h>
#include <httplib.h>

#include "common/logging/log.h"
#include "core/online_initiator.h"
#include "core/telemetry_session.h"
#include "ui_configure_online.h"
#include "yuzu/configuration/configure_online.h"
#include "yuzu/online/friends.h"
#include "yuzu/online/monitor.h"
#include "yuzu/online/online_util.h"
#include "yuzu/uisettings.h"

namespace {

constexpr int AVATAR_MIN_SIZE = 256;
constexpr char TOKEN_DELIMITER = ':';

[[nodiscard]] std::optional<httplib::Headers> AuthorizationHeaders(
    Core::OnlineInitiator* online_initiator) {
    const std::optional id_token = online_initiator->LoadIdTokenApp("profile");
    if (!id_token) {
        return std::nullopt;
    }
    return httplib::Headers{
        {"Authorization", "Bearer " + id_token.value()},
    };
}

} // Anonymous namespace

ConfigureOnline::ConfigureOnline(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureOnline>()), verify_watcher(this),
      online_username_watcher(this), online_avatar_watcher(this), upload_username_watcher(this),
      upload_avatar_watcher(this), subscription_info_watcher(this) {
    ui->setupUi(this);
}

ConfigureOnline::~ConfigureOnline() {
    // Qt doesn't delay destruction until watchers finish, we have to do it ourselves
    verify_watcher.waitForFinished();
    online_username_watcher.waitForFinished();
    online_avatar_watcher.waitForFinished();
    upload_username_watcher.waitForFinished();
    upload_avatar_watcher.waitForFinished();
}

void ConfigureOnline::Initialize(Core::OnlineInitiator& online_initiator_,
                                 OnlineStatusMonitor* online_status_monitor_,
                                 FriendsList* friend_list_) {
    online_initiator = &online_initiator_;
    online_status_monitor = online_status_monitor_;
    friend_list = friend_list_;

    profile_scene = new QGraphicsScene;
    ui->online_profile_image->setScene(profile_scene);

    ui->button_set_username->setEnabled(false);
    ui->button_set_avatar->setEnabled(false);

    connect(ui->button_set_username, &QPushButton::clicked, this, &ConfigureOnline::SetUserName);
    connect(ui->button_set_avatar, &QPushButton::clicked, this, &ConfigureOnline::SetAvatar);
    connect(ui->buttonSubscription, &QPushButton::clicked, this,
            &ConfigureOnline::ManageSubscription);
    connect(ui->buttonLogout, &QPushButton::clicked, this, &ConfigureOnline::AccountLogout);
    /// Connections for fteching subscription info
    connect(&verify_watcher, &QFutureWatcher<bool>::finished, this,
            &ConfigureOnline::OnLoginVerified);
    connect(&online_username_watcher, &QFutureWatcher<std::optional<std::string>>::finished, this,
            &ConfigureOnline::OnOnlineUserNameRefreshed);
    connect(&online_avatar_watcher, &QFutureWatcher<QImage>::finished, this,
            &ConfigureOnline::OnOnlineAvatarRefreshed);
    connect(&upload_username_watcher, &QFutureWatcher<int>::finished, this,
            &ConfigureOnline::OnUserNameUploaded);
    connect(&upload_avatar_watcher, &QFutureWatcher<int>::finished, this,
            &ConfigureOnline::OnAvatarUploaded);
    connect(&subscription_info_watcher, &QFutureWatcher<Core::OnlineSubscriptionInfo>::finished,
            this, &ConfigureOnline::OnRefreshSubscription);

#ifndef USE_DISCORD_PRESENCE
    ui->discord_group->setVisible(false);
#endif

    SetConfiguration();
    RetranslateUI();
    OnLoginVerified();
}

void ConfigureOnline::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureOnline::RetranslateUI() {
    ui->retranslateUi(this);
}

void ConfigureOnline::SetConfiguration() {
    ui->toggle_discordrpc->setChecked(UISettings::values.enable_discord_presence);
    ui->groupBoxOnline->setHidden(Settings::values.raptor_token.empty());
    ui->groupBoxOffline->setHidden(!Settings::values.raptor_token.empty());
}

void ConfigureOnline::ApplyConfiguration() {
    UISettings::values.enable_discord_presence = ui->toggle_discordrpc->isChecked();
}

void ConfigureOnline::OnLoginVerified() {
    SetConfiguration();
    RefreshOnlineAvatar();
    RefreshOnlineUserName();
    RefreshSubscription();
    online_initiator->Disconnect();
    online_initiator->Connect();
    online_status_monitor->Refresh();
}

void ConfigureOnline::RefreshOnlineUserName() {
    if (Settings::values.raptor_token.empty()) {
        ui->label_online_username->setText(tr("Unspecified"));
        ui->label_online_username->setDisabled(true);
        return;
    }

    ui->label_online_username->setDisabled(true);
    ui->label_online_username->setText(tr("Refreshing..."));
    online_username_watcher.setFuture(QtConcurrent::run([this]() -> std::optional<std::string> {
        const std::optional headers = AuthorizationHeaders(online_initiator);
        if (!headers) {
            return std::nullopt;
        }
        httplib::SSLClient client(online_initiator->ProfileApiUrl());
        auto response = client.Get("/api/v1/username", *headers);
        if (!response || response->status != 200) {
            LOG_ERROR(Frontend, "Failed to query username from server");
            return std::nullopt;
        }
        return response->body;
    }));
}

void ConfigureOnline::OnOnlineUserNameRefreshed() {
    if (const std::optional<std::string> result = online_username_watcher.result(); result) {
        ui->label_online_username->setText(QString::fromStdString(*result));
        ui->label_online_username->setDisabled(false);
        ui->button_set_username->setEnabled(true);
    } else {
        ui->label_online_username->setText(tr("Unspecified"));
        ui->label_online_username->setDisabled(true);
        ui->button_set_username->setEnabled(false);
    }
}

void ConfigureOnline::RefreshOnlineAvatar(std::chrono::seconds delay) {
    profile_scene->clear();
    if (Settings::values.raptor_token.empty()) {
        return;
    }

    profile_scene->addItem(
        new QGraphicsPixmapItem(QIcon::fromTheme(QStringLiteral("portrait_sync")).pixmap(48)));

    online_avatar_watcher.setFuture(QtConcurrent::run([this, delay]() -> QImage {
        const std::optional headers = AuthorizationHeaders(online_initiator);
        if (!headers) {
            return {};
        }
        std::this_thread::sleep_for(delay);
        httplib::SSLClient client(online_initiator->ProfileApiUrl());
        client.set_follow_location(true);
        auto response = client.Get("/api/v1/avatar/64/64", *headers);
        if (!response || response->status != 200) {
            LOG_ERROR(Frontend, "Failed to querying profile image");
            return {};
        }

        return QImage::fromData(reinterpret_cast<const uchar*>(response->body.data()),
                                static_cast<int>(response->body.size()));
    }));
}

void ConfigureOnline::OnOnlineAvatarRefreshed() {
    profile_scene->clear();

    QImage image = online_avatar_watcher.result();
    if (image.isNull()) {
        QPixmap pixmap = QIcon::fromTheme(QStringLiteral("avatar-sync-error")).pixmap(48);
        profile_scene->addItem(new QGraphicsPixmapItem(pixmap));
        ui->button_set_avatar->setEnabled(false);
        return;
    }

    QPixmap pixmap =
        QPixmap::fromImage(image).scaled(48, 48, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    profile_scene->addItem(new QGraphicsPixmapItem(pixmap));
    ui->button_set_avatar->setEnabled(true);
}

void ConfigureOnline::SetUserName() {
    QString old_username = ui->label_online_username->text();
    bool ok = false;
    QString new_username = QInputDialog::getText(this, tr("Set Username"), tr("Username:"),
                                                 QLineEdit::Normal, old_username, &ok);
    if (!ok || old_username == new_username) {
        return;
    }

    button_set_username_text = ui->button_set_username->text();
    ui->button_set_username->setEnabled(false);
    ui->button_set_username->setText(tr("Uploading..."));

    upload_username_watcher.setFuture(QtConcurrent::run([this, name = new_username.toStdString()] {
        const std::optional headers = AuthorizationHeaders(online_initiator);
        if (!headers) {
            return -1;
        }
        httplib::SSLClient client(online_initiator->ProfileApiUrl());
        auto response = client.Put("/api/v1/username", *headers, name, "text/plain");
        return response ? response->status : -1;
    }));
}

void ConfigureOnline::OnUserNameUploaded() {
    ui->button_set_username->setEnabled(true);
    ui->button_set_username->setText(button_set_username_text);

    switch (upload_username_watcher.result()) {
    case 200:
        RefreshOnlineUserName();
        break;
    case 400:
        QMessageBox::critical(this, tr("Set Username"), tr("Invalid username."));
        break;
    default:
        QMessageBox::critical(this, tr("Set Username"), tr("Failed to update username."));
        break;
    }
}

void ConfigureOnline::SetAvatar() {
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Select Avatar"), QString(),
        tr("JPEG Images (*.jpg *.jpeg);;PNG Images (*.png);;BMP Images (*.bmp)"));
    if (file.isEmpty()) {
        return;
    }

    QPixmap source(file);
    const QSize size = source.size();
    if (size.width() < AVATAR_MIN_SIZE || size.height() < AVATAR_MIN_SIZE) {
        const auto reply = QMessageBox::warning(
            this, tr("Select Avatar"),
            tr("Selected image is smaller than %1 pixels.\n"
               "Images with the same width and height and larger than %2 pixels are recommended. "
               "That said, yuzu will scale the image and add white borders.\n\n"
               "Do you want to proceed?")
                .arg(AVATAR_MIN_SIZE)
                .arg(AVATAR_MIN_SIZE),
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    } else if (size.width() != size.height()) {
        const auto reply =
            QMessageBox::warning(this, tr("Select Avatar"),
                                 tr("Selected image is not squared.\n"
                                    "Images with the same width and height are recommended. That "
                                    "said, yuzu will adjust the image adding white borders.\n\n"
                                    "Do you want to proceed?"),
                                 QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    button_set_avatar_text = ui->button_set_avatar->text();
    ui->button_set_avatar->setEnabled(false);
    ui->button_set_avatar->setText(tr("Uploading..."));

    upload_avatar_watcher.setFuture(QtConcurrent::run([this, source] {
        const QSize size = source.size();
        const int max = std::max(size.width(), size.height());
        const int dim = std::max(max, AVATAR_MIN_SIZE);

        QPixmap pixmap(QSize(dim, dim));
        pixmap.fill(QColor(255, 255, 255));

        QTransform transform;
        const qreal scale = static_cast<qreal>(dim) / static_cast<qreal>(max);
        transform.scale(scale, scale);

        QPainter painter(&pixmap);
        painter.setTransform(transform);
        const QPoint draw_pos((dim - size.width() * scale) / 2, (dim - size.height() * scale) / 2);
        painter.drawPixmap(draw_pos / scale, source);

        QByteArray byte_array;
        QBuffer buffer(&byte_array);
        buffer.open(QIODevice::WriteOnly);
        pixmap.save(&buffer, "JPEG");
        std::string jpeg_string = byte_array.toStdString();

        const std::optional headers = AuthorizationHeaders(online_initiator);
        if (!headers) {
            return -1;
        }
        httplib::SSLClient client(online_initiator->ProfileApiUrl());
        auto response = client.Put("/api/v1/avatar", *headers, jpeg_string, "image/jpeg");
        return response ? response->status : -1;
    }));
}

void ConfigureOnline::OnAvatarUploaded() {
    ui->button_set_avatar->setEnabled(true);
    ui->button_set_avatar->setText(button_set_avatar_text);

    switch (upload_avatar_watcher.result()) {
    case 200:
    case 202:
        RefreshOnlineAvatar(std::chrono::seconds{5});
        break;
    case 400:
        QMessageBox::critical(this, tr("Set Avatar"), tr("Invalid avatar."));
        break;
    case 429:
        QMessageBox::critical(this, tr("Set Avatar"), tr("Avatar has been set too recently."));
        break;
    default:
        QMessageBox::critical(this, tr("Set Avatar"), tr("Failed to upload avatar."));
        break;
    }
}

void ConfigureOnline::AccountLogout() {
    Settings::values.raptor_token.clear();
    online_initiator->Connect();
    OnLoginVerified();
}

void ConfigureOnline::ManageSubscription() {
    if (!url_subscription_action.empty()) {
        QDesktopServices::openUrl(QUrl(QString::fromStdString(url_subscription_action)));
    }
}

void ConfigureOnline::RefreshSubscription() {
    subscription_info_watcher.setFuture(
        QtConcurrent::run([this] { return online_initiator->GetSubscriptionInfo(); }));
}

void ConfigureOnline::OnRefreshSubscription() {
    subscription_info = subscription_info_watcher.result();

    ui->labelCurrentSubscription->setText(
        QString::fromStdString(subscription_info.display_subscription));
    ui->buttonSubscription->setText(QString::fromStdString(subscription_info.display_action));
    url_subscription_action = subscription_info.url_action;
    ui->labelRaptorSubWarn->setHidden(subscription_info.enable_set_username &&
                                      subscription_info.enable_set_profile);
    ui->button_set_avatar->setEnabled(subscription_info.enable_set_profile);
    ui->button_set_username->setEnabled(subscription_info.enable_set_username);
    ui->labelSubscriptionNotice->setHidden(!subscription_info.show_subscription_upgrade_notice);
}
