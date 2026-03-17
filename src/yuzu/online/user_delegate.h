// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <QFutureWatcher>
#include <QPixmap>
#include <QStandardItem>
#include <QString>
#include <QStyledItemDelegate>

#include "yuzu/online/types.h"

class UserInfo : public QObject {
    Q_OBJECT

    enum class Mode {
        Friend,
        IncomingRequest,
        OutgoingRequest,
        BlockedUser,
    };

public:
    explicit UserInfo(const std::string& key, const Friend& friend_data, QWidget* paintable);
    explicit UserInfo(const std::string& key, const FriendRequest& request, bool is_incoming,
                      QWidget* paintable);
    explicit UserInfo(const std::string& key, const BlockedUser& blocked_user, QWidget* paintable);

    [[nodiscard]] const std::string& Key() const noexcept {
        return item_key;
    }

    [[nodiscard]] bool ShowConnected() const {
        return mode == Mode::Friend;
    }

    QString username;
    QString friend_code;
    QString title_name;
    QPixmap avatar;

    std::atomic_bool has_avatar{false};
    bool is_connected = false;

private:
    void AsynchronousDownloadAvatar(std::string avatar_url);

    void OnAvatarLoaded();

    QFutureWatcher<QImage> future_watcher;

    const Mode mode;
    const std::string item_key;
    QWidget* paintable;
};
Q_DECLARE_METATYPE(UserInfo*)

class UserStandardItem : public QStandardItem {
public:
    using QStandardItem::QStandardItem;

    bool operator<(const QStandardItem& other) const override;

private:
};

class UserDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit UserDelegate(QWidget* parent_, std::initializer_list<const char*> actions);

signals:
    void ButtonPressed(int item_index, int action_index);

protected:
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;

private:
    [[nodiscard]] QRect ActionRectangle(const QRect& rect, int button_index) const;

    [[nodiscard]] std::optional<int> HandleMouseEvent(QRect option_rect, QPoint point);

    QWidget* const parent;

    const QPixmap pixmap_online;
    const QPixmap pixmap_offline;
    const QPixmap pixmap_avatar_loading;

    const int num_actions;

    std::vector<QPixmap> pixmap_icons;
    std::vector<QPixmap> pixmap_hover_icons;

    int hover_item_index = -1;
    int hover_action_index = -1;
};
