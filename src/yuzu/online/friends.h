// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include <QDialog>
#include <QFutureWatcher>
#include <QPixmap>

#include "yuzu/online/types.h"

namespace Core {
class OnlineInitiator;
}

namespace httplib {
struct Response;
}

namespace Ui {
class Friends;
}

class QGraphicsScene;
class QStandardItemModel;
class QWidget;

class FriendsList : public QDialog {
    Q_OBJECT

public:
    explicit FriendsList(Core::OnlineInitiator& online_initiator, QWidget* parent = nullptr);
    ~FriendsList() override;

    void Reload();

    void Refresh();

signals:
    void ChangedAirplaneMode();

private slots:
    void OnChangedAirplaneMode();

    void OnDownloadFinished();

    void OnFriendRemovePressed(int user_index, int button_index);
    void OnRemoveFriendFinished(int result);
    void OnBlockFriendFinished(int result);

    void OnAddFriend();
    void OnAddFriendFinished(int result);

    void OnIncomingRequestPressed(int user_index, int button_index);
    void OnIncomingRequestChangedFinished(int result);

    void OnSentRequestCancel(int user_index, int button_index);
    void OnSentRequestCancelFinished(int result);

    void OnBlockUser();
    void OnBlockUserFinished(int result);

    void OnUnblockUser(int user_index, int button_index);
    void OnUnblockUserFinished(int result);

    void OnCopyFriendCode();

    void OnValidateFriendCode(QWidget* button, const QString& code);
    void OnFriendsListClicked();
    void OnFriendsAddClicked();
    void OnBlockUsersClicked();

private:
    struct State {
        std::string username;
        QPixmap avatar;
        std::string friend_code;
        bool online_status = false;
        std::string title_name;
        std::unordered_map<std::string, Friend> friends;
        std::unordered_map<std::string, FriendRequest> sent_requests;
        std::unordered_map<std::string, FriendRequest> incoming_requests;
        std::unordered_map<std::string, BlockedUser> blocked_users;
    };

    void Disable();
    void Enable();

    void ClearState();
    void ClearWidgets();

    void EnableActions(bool state);

    void RefreshState();
    void RefreshUserState();
    void RefreshFriendListState();
    void RefreshSentRequests();
    void RefreshIncomingRequests();
    void RefreshBlockedUsers();

    [[nodiscard]] bool Download() noexcept;

    // Qt's parser doesn't like C++20 concepts on Linux
    // template <typename QtFunc, std::invocable<> AsyncFunc>
    template <typename QtFunc, typename AsyncFunc>
    void AsyncOperation(std::string_view context, QtFunc&& qt_func, AsyncFunc&& async_func);

    Core::OnlineInitiator& online_initiator;

    std::unique_ptr<Ui::Friends> ui;

    QFutureWatcher<bool> download_future;

    QGraphicsScene* user_avatar_scene = nullptr;
    QStandardItemModel* friend_list_model = nullptr;
    QStandardItemModel* sent_requests_model = nullptr;
    QStandardItemModel* incoming_requests_model = nullptr;
    QStandardItemModel* blocked_users_model = nullptr;

    QFutureWatcher<int> operation_future;
    QMetaObject::Connection operation_connection;

    std::mutex state_mutex;
    State state;
    State old_state;
};
