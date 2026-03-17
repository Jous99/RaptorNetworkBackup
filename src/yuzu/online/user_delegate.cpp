// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <optional>
#include <string>
#include <thread>

#include <QImage>
#include <QMouseEvent>
#include <QPainter>

#include <QtConcurrent/QtConcurrentRun>

#include <fmt/format.h>

#include "yuzu/online/online_util.h"
#include "yuzu/online/types.h"
#include "yuzu/online/user_delegate.h"

namespace {

constexpr int ROLE = Qt::UserRole + 1;

constexpr int AVATAR_SIZE = 48;
constexpr int BORDER_SIZE = 2;
constexpr int ICON_SIZE = 20;
constexpr int TEXT_SPACING = 6;
constexpr int TEXT_LEFT = BORDER_SIZE + AVATAR_SIZE + TEXT_SPACING;

constexpr int TEXT_FLAGS = Qt::AlignTop | Qt::TextSingleLine;

QPixmap LoadPixmap(const char* name, int extent) {
    return QIcon::fromTheme(QString::fromUtf8(name)).pixmap(extent);
}

} // Anonymous namespace

UserInfo::UserInfo(const std::string& key, const Friend& friend_data, QWidget* paintable_)
    : username(QString::fromStdString(friend_data.username)),
      friend_code(QString::fromStdString(friend_data.friend_code)),
      title_name(QString::fromStdString(friend_data.status_title_name)),
      is_connected(friend_data.status_code), future_watcher(this), mode(Mode::Friend),
      item_key(key), paintable(paintable_) {
    AsynchronousDownloadAvatar(friend_data.avatar_url);
}

UserInfo::UserInfo(const std::string& key, const FriendRequest& request, bool is_incoming,
                   QWidget* paintable_)
    : username(QString::fromStdString(is_incoming ? request.sender_username
                                                  : request.receiver_username)),
      friend_code(QString::fromStdString(is_incoming ? request.sender_friend_code
                                                     : request.receiver_friend_code)),
      future_watcher(this), mode(is_incoming ? Mode::IncomingRequest : Mode::OutgoingRequest),
      item_key(key), paintable(paintable_) {
    AsynchronousDownloadAvatar(is_incoming ? request.sender_avatar_url
                                           : request.receiver_avatar_url);
}

UserInfo::UserInfo(const std::string& key, const BlockedUser& blocked_user, QWidget* paintable_)
    : username(QString::fromStdString(blocked_user.username)),
      friend_code(QString::fromStdString(blocked_user.friend_code)), future_watcher(this),
      mode(Mode::BlockedUser), item_key(key), paintable(paintable_) {
    AsynchronousDownloadAvatar(blocked_user.avatar_url);
}

void UserInfo::AsynchronousDownloadAvatar(std::string avatar_url) {
    connect(&future_watcher, &QFutureWatcher<QImage>::finished, this, &UserInfo::OnAvatarLoaded);
    future_watcher.setFuture(QtConcurrent::run([avatar_url] {
        std::this_thread::sleep_for(std::chrono::seconds{5});
        return DownloadImageUrl(AvatarUrl(avatar_url, "64"));
    }));
}

void UserInfo::OnAvatarLoaded() {
    const QImage image = future_watcher.result();
    if (image.isNull()) {
        avatar = LoadPixmap("portrait_sync_error", AVATAR_SIZE);
    } else {
        avatar = QPixmap::fromImage(image).scaled(QSize(AVATAR_SIZE, AVATAR_SIZE),
                                                  Qt::AspectRatioMode::IgnoreAspectRatio,
                                                  Qt::TransformationMode::SmoothTransformation);
    }
    has_avatar = true;

    paintable->repaint();
}

bool UserStandardItem::operator<(const QStandardItem& other) const {
    const auto lhs = data(ROLE).value<UserInfo*>();
    const auto rhs = other.data(ROLE).value<UserInfo*>();
    if (lhs->is_connected == rhs->is_connected) {
        return lhs->username.toUpper() > rhs->username.toUpper();
    }
    return lhs->is_connected < rhs->is_connected;
}

UserDelegate::UserDelegate(QWidget* parent_, std::initializer_list<const char*> actions)
    : parent(parent_), pixmap_online(LoadPixmap("user_online", 12)),
      pixmap_offline(LoadPixmap("user_offline", 12)),
      pixmap_avatar_loading(LoadPixmap("portrait_sync", AVATAR_SIZE)),
      num_actions(static_cast<int>(actions.size())), pixmap_icons(num_actions),
      pixmap_hover_icons(num_actions) {
    std::transform(actions.begin(), actions.end(), pixmap_icons.begin(),
                   [](const char* name) { return LoadPixmap(name, ICON_SIZE); });
    std::transform(actions.begin(), actions.end(), pixmap_hover_icons.begin(),
                   [](const char* name) {
                       return LoadPixmap(fmt::format("{}_hover", name).c_str(), ICON_SIZE);
                   });
}

void UserDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                         const QModelIndex& index) const {
    QStyledItemDelegate::paint(painter, option, index);

    const UserInfo* const info = index.data(ROLE).value<UserInfo*>();
    const QRect rect = option.rect;

    painter->drawPixmap(rect.x() + BORDER_SIZE, rect.y() + BORDER_SIZE, AVATAR_SIZE, AVATAR_SIZE,
                        info->has_avatar ? info->avatar : pixmap_avatar_loading);

    QFont font = painter->font();
    font.setPixelSize(12);
    font.setBold(false);
    painter->setFont(font);

    if (info->ShowConnected()) {
        const QString text_status =
            info->is_connected ? tr("Playing %1").arg(info->title_name) : tr("Offline");
        const QPixmap& pixmap_status = info->is_connected ? pixmap_online : pixmap_offline;
        painter->drawPixmap(rect.x() + BORDER_SIZE + AVATAR_SIZE + 6, rect.y() + 30, pixmap_status);
        painter->drawText(rect.x() + BORDER_SIZE + AVATAR_SIZE + 22, rect.y() + 40, text_status);
    }

    font.setBold(true);
    painter->setFont(font);

    QRect bounding_box;
    const int text_x = rect.x() + TEXT_LEFT + (info->ShowConnected() ? 0 : 2);
    const int text_y = rect.y() + TEXT_SPACING + (info->ShowConnected() ? 2 : 0);
    const QRect text_rect = QRect(text_x, text_y, rect.width(), rect.height()).intersected(rect);
    painter->drawText(text_rect, TEXT_FLAGS, info->username, &bounding_box);

    if ((option.state & (QStyle::State_Selected | QStyle::State_MouseOver)) &&
        (option.state & QStyle::State_Enabled) != 0) {
        for (int action_index = 0; action_index < num_actions; ++action_index) {
            const bool is_hover =
                hover_item_index == index.row() && hover_action_index == action_index;
            const QPixmap& pixmap =
                is_hover ? pixmap_hover_icons[action_index] : pixmap_icons[action_index];
            painter->drawPixmap(ActionRectangle(rect, action_index), pixmap);
        }
    }
}

QSize UserDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const QSize rect = QStyledItemDelegate::sizeHint(option, index);
    return QSize(rect.width(), AVATAR_SIZE + BORDER_SIZE * 2);
}

bool UserDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                               const QStyleOptionViewItem& option, const QModelIndex& index) {
    switch (event->type()) {
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease: {
        const auto mouse_event = static_cast<QMouseEvent*>(event);
        const QPoint mouse_point = mouse_event->localPos().toPoint();
        const Qt::MouseButton mouse_button = mouse_event->button();

        const std::optional<int> action_index = HandleMouseEvent(option.rect, mouse_point);
        const int item_index = index.row();
        [[maybe_unused]] bool changed = false;
        if (action_index) {
            if (event->type() == QEvent::MouseButtonRelease && mouse_button == Qt::LeftButton) {
                ButtonPressed(item_index, *action_index);
            }
        }
        if (hover_item_index != item_index || hover_action_index != action_index.value_or(-1)) {
            hover_item_index = item_index;
            hover_action_index = action_index.value_or(-1);
            parent->update();
        }
        return true;
    }
    default:
        break;
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

std::optional<int> UserDelegate::HandleMouseEvent(QRect option_rect, QPoint point) {
    for (int action_index = 0; action_index < num_actions; ++action_index) {
        if (!ActionRectangle(option_rect, action_index).contains(point)) {
            continue;
        }
        return action_index;
    }
    return std::nullopt;
}

QRect UserDelegate::ActionRectangle(const QRect& rect, int action_index) const {
    static constexpr int y_spacing = BORDER_SIZE + ICON_SIZE;
    const int x = rect.x() + rect.width() - BORDER_SIZE - ICON_SIZE - 2;
    const int y = rect.y() + BORDER_SIZE + y_spacing * action_index + 2;
    return QRect(x, y, ICON_SIZE, ICON_SIZE);
}
