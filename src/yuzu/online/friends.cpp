// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <QClipboard>
#include <QDialog>
#include <QGraphicsScene>
#include <QListView>
#include <QMessageBox>
#include <QMouseEvent>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QToolTip>

#include <QtConcurrent/QtConcurrentRun>

#include <httplib.h>

#include <nlohmann/json.hpp>

#include "common/logging/log.h"
#include "common/settings.h"
#include "core/online_initiator.h"
#include "ui_friends.h"
#include "yuzu/clickable_icon.h"
#include "yuzu/online/friends.h"
#include "yuzu/online/online_util.h"
#include "yuzu/online/types.h"
#include "yuzu/online/user_delegate.h"

namespace {

constexpr int ROLE_INDEX = Qt::UserRole + 1;

enum class RequestMethod {
    Get,
    Post,
    Put,
    Delete,
};

[[nodiscard]] httplib::Headers AuthorizationHeaders(Core::OnlineInitiator& online_initiator,
                                                    const char* app_name) {
    const std::optional id_token = online_initiator.LoadIdTokenApp(app_name);
    if (!id_token) {
        throw std::runtime_error{"Failed to query application token"};
    }
    return httplib::Headers{
        {"Authorization", "Bearer " + id_token.value()},
    };
}

[[nodiscard]] httplib::Result Request(Core::OnlineInitiator& online_initiator, RequestMethod method,
                                      const char* url, const std::string& contents = "") {
    const httplib::Headers headers = AuthorizationHeaders(online_initiator, "friends");
    httplib::SSLClient client(online_initiator.FriendsApiUrl());
    switch (method) {
    case RequestMethod::Get:
        return client.Get(url, headers);
    case RequestMethod::Post:
        return client.Post(url, headers, contents, contents.empty() ? "" : "text/plain");
    case RequestMethod::Put:
        return client.Put(url, headers, contents, contents.empty() ? "" : "text/plain");
    case RequestMethod::Delete:
        return client.Delete(url, headers);
    }
    throw std::logic_error{"Invalid HTTP request method"};
}

[[nodiscard]] bool ConvertStatus(int status) {
    switch (status) {
    case 0:
        return false;
    case 1:
        return true;
    default:
        throw std::runtime_error(fmt::format("Invalid online status: '{}'", status));
    }
}

void CheckValidResponse(const httplib::Result& response, std::string_view source) {
    if (!response) {
        throw std::runtime_error(fmt::format("No response from '{}'", source));
    }
    if (response->status != 200) {
        throw std::runtime_error(fmt::format("Error from '{}': {}", source, response->status));
    }
}

[[nodiscard]] int RemoveFriend(Core::OnlineInitiator& online_initiator,
                               std::string_view friend_code) {
    auto response = Request(online_initiator, RequestMethod::Delete,
                            fmt::format("/api/v1/me/friend/{}", friend_code).c_str());
    return response ? response->status : -1;
}

[[nodiscard]] int AddFriend(Core::OnlineInitiator& online_initiator, std::string_view friend_code) {
    auto response = Request(online_initiator, RequestMethod::Post,
                            fmt::format("/api/v1/request/out/{}", friend_code).c_str());
    return response ? response->status : -1;
}

[[nodiscard]] int ChangeFriendRequest(Core::OnlineInitiator& online_initiator,
                                      std::string_view request_id, bool accept) {
    auto response =
        Request(online_initiator, RequestMethod::Post,
                fmt::format("/api/v1/request/in/{}", request_id).c_str(), accept ? "1" : "0");
    return response ? response->status : -1;
}

[[nodiscard]] int CancelSentFriendRequest(Core::OnlineInitiator& online_initiator,
                                          std::string_view request_id) {
    auto response = Request(online_initiator, RequestMethod::Delete,
                            fmt::format("/api/v1/request/out/{}", request_id).c_str());
    return response ? response->status : -1;
}

[[nodiscard]] int BlockUser(Core::OnlineInitiator& online_initiator, std::string_view friend_code) {
    auto response = Request(online_initiator, RequestMethod::Put,
                            fmt::format("/api/v1/block/{}", friend_code).c_str());
    return response ? response->status : -1;
}

[[nodiscard]] int UnblockUser(Core::OnlineInitiator& online_initiator,
                              std::string_view friend_code) {
    auto response = Request(online_initiator, RequestMethod::Delete,
                            fmt::format("/api/v1/block/{}", friend_code).c_str());
    return response ? response->status : -1;
}

[[nodiscard]] std::string GetFriendCode(const httplib::Result& response) {
    CheckValidResponse(response, __FUNCTION__);
    return response->body;
}

[[nodiscard]] bool GetOnlineStatus(const httplib::Result& response) {
    CheckValidResponse(response, __FUNCTION__);
    const std::string_view body = response->body;
    const std::string_view status = body.substr(0, body.find_first_of(':'));
    if (status == "0") {
        return false;
    } else if (status == "1") {
        return true;
    } else {
        throw std::runtime_error{fmt::format("Invalid online status={}", status)};
    }
}

[[nodiscard]] std::string GetCurrentTitleName(const httplib::Result& response) {
    CheckValidResponse(response, __FUNCTION__);
    const std::string_view body = response->body;
    return std::string(body.substr(body.find_first_of(':') + 1));
}

[[nodiscard]] std::unordered_map<std::string, Friend> GetFriendList(
    const httplib::Result& response) {
    CheckValidResponse(response, __FUNCTION__);
    const nlohmann::json json = nlohmann::json::parse(response->body);
    std::unordered_map<std::string, Friend> friends;
    friends.reserve(json.size());
    for (const auto& entry : json) {
        const auto title_name = entry.find("status_title_name");
        friends.emplace(entry.at("friend_code"),
                        Friend{
                            .account_id = entry.at("pid"),
                            .friend_code = entry.at("friend_code"),
                            .username = entry.at("username"),
                            .avatar_url = entry.at("avatar_url"),
                            .status_title_name = title_name != entry.end() ? *title_name : "",
                            .status_code = ConvertStatus(entry.at("status_code")),
                        });
    }
    return friends;
}

[[nodiscard]] FriendRequest::Status ConvertRequestStatus(int status) {
    switch (status) {
    case 0:
        return FriendRequest::Status::Normal;
    case 1:
        return FriendRequest::Status::RetractedBySender;
    case 2:
        return FriendRequest::Status::RejectedByRecipient;
    default:
        throw std::runtime_error(fmt::format("Invalid friend request status: '{}'", status));
    }
}

[[nodiscard]] std::unordered_map<std::string, FriendRequest> GetRequests(
    const httplib::Result& response) {
    CheckValidResponse(response, __FUNCTION__);
    const nlohmann::json json = nlohmann::json::parse(response->body);
    std::unordered_map<std::string, FriendRequest> requests;
    requests.reserve(json.size());
    for (const auto& entry : json) {
        requests.emplace(entry.at("request_id"),
                         FriendRequest{
                             .request_id = entry.at("request_id"),
                             .sender_username = entry.at("sender_username"),
                             .sender_friend_code = entry.at("sender_friend_code"),
                             .sender_avatar_url = entry.at("sender_avatar_url"),
                             .receiver_username = entry.at("receiver_username"),
                             .receiver_friend_code = entry.at("receiver_friend_code"),
                             .receiver_avatar_url = entry.at("receiver_avatar_url"),
                             .status = ConvertRequestStatus(entry.at("status")),
                         });
    };
    return requests;
}

[[nodiscard]] std::unordered_map<std::string, BlockedUser> GetBlockedUsers(
    const httplib::Result& response) {
    CheckValidResponse(response, __FUNCTION__);
    const nlohmann::json json = nlohmann::json::parse(response->body);
    std::unordered_map<std::string, BlockedUser> blocked_users;
    blocked_users.reserve(json.size());
    for (const auto& entry : json) {
        blocked_users.emplace(entry.at("blocked_friend_code"),
                              BlockedUser{
                                  .account_id = entry.at("blocked_account_id"),
                                  .friend_code = entry.at("blocked_friend_code"),
                                  .username = entry.at("blocked_username"),
                                  .avatar_url = entry.at("blocked_avatar_url"),
                              });
    }
    return blocked_users;
}

[[nodiscard]] std::string GetUsername(const httplib::Result& response) {
    CheckValidResponse(response, __FUNCTION__);
    return response->body;
}

[[nodiscard]] QPixmap GetAvatar(const httplib::Result& response) {
    if (!response) {
        throw std::runtime_error{"Failed to query avatar from server"};
    }
    return QPixmap::fromImage(
               QImage::fromData(reinterpret_cast<const uchar*>(response->body.data()),
                                static_cast<int>(response->body.size())))
        .scaled(48, 48, Qt::IgnoreAspectRatio, Qt::TransformationMode::SmoothTransformation);
}

template <class Container, class Func>
void RefreshModel(const Container& new_entries, const Container& old_entries,
                  QStandardItemModel* model, Func&& func) {
    std::vector<typename Container::const_iterator> added_entries;
    std::vector<typename Container::const_iterator> removed_entries;

    for (auto it = new_entries.cbegin(); it != new_entries.cend(); ++it) {
        if (!old_entries.contains(it->first)) {
            added_entries.push_back(it);
        }
    }
    for (auto it = old_entries.cbegin(); it != old_entries.cend(); ++it) {
        if (!new_entries.contains(it->first)) {
            removed_entries.push_back(it);
        }
    }

    for (const auto new_entry : added_entries) {
        auto item = new UserStandardItem;
        item->setData(QVariant::fromValue(func(new_entry->first, new_entry->second)));
        model->appendRow(item);
    }

    for (int item_index = 0; item_index < model->rowCount();) {
        const QModelIndex index = model->index(item_index, 0);
        UserInfo* const info = model->data(index, ROLE_INDEX).value<UserInfo*>();
        const bool is_removed =
            std::ranges::find_if(removed_entries, [info](const auto removed_entry) {
                return info->Key() == removed_entry->first;
            }) != removed_entries.end();
        if (is_removed) {
            model->removeRow(item_index);
            continue;
        }
        if constexpr (std::is_same_v<typename Container::mapped_type, Friend>) {
            const Friend& entry = new_entries.at(info->Key());
            info->is_connected = entry.status_code;
            info->title_name = QString::fromStdString(entry.status_title_name);
        }
        ++item_index;
    }

    model->sort(0, Qt::DescendingOrder);
}

[[nodiscard]] bool IsFriendCodeValid(const QString& code) {
    return code.size() == 11 && code[3] == QChar::fromLatin1('-') &&
           code[7] == QChar::fromLatin1('-');
}

} // Anonymous namespace

FriendsList::FriendsList(Core::OnlineInitiator& online_initiator_, QWidget* parent)
    : QDialog(parent), online_initiator(online_initiator_), ui(std::make_unique<Ui::Friends>()),
      download_future(this), operation_future(this) {
    ui->setupUi(this);

    user_avatar_scene = new QGraphicsScene;
    ui->online_profile_image->setScene(user_avatar_scene);

    QListView* const list_view_friends = ui->list_view_friends;
    const auto friend_list_delegate =
        new UserDelegate(list_view_friends, {"user_remove", "user_block"});
    friend_list_model = new QStandardItemModel(list_view_friends);
    list_view_friends->setItemDelegate(friend_list_delegate);
    list_view_friends->setModel(friend_list_model);

    QListView* const list_sent_requests = ui->list_view_sent_requests;
    const auto sent_requests_delegate = new UserDelegate(list_sent_requests, {"delete"});
    sent_requests_model = new QStandardItemModel(list_sent_requests);
    list_sent_requests->setItemDelegate(sent_requests_delegate);
    list_sent_requests->setModel(sent_requests_model);

    QListView* const list_incoming_requests = ui->list_view_incoming_requests;
    const auto incoming_requests_delegate =
        new UserDelegate(list_sent_requests, {"choose_yes", "choose_no"});
    incoming_requests_model = new QStandardItemModel(list_incoming_requests);
    list_incoming_requests->setItemDelegate(incoming_requests_delegate);
    list_incoming_requests->setModel(incoming_requests_model);

    QListView* const list_blocked_users = ui->list_view_blocked_users;
    const auto blocked_users_delegate = new UserDelegate(list_blocked_users, {"delete"});
    blocked_users_model = new QStandardItemModel(list_blocked_users);
    list_blocked_users->setItemDelegate(blocked_users_delegate);
    list_blocked_users->setModel(blocked_users_model);

    connect(this, &FriendsList::ChangedAirplaneMode, &FriendsList::OnChangedAirplaneMode);

    connect(ui->icon_friends_add, &ClickableIcon::activated, this,
            &FriendsList::OnFriendsAddClicked);
    connect(ui->icon_friends_add_2, &ClickableIcon::activated, this,
            &FriendsList::OnFriendsAddClicked);
    connect(ui->icon_block_users, &ClickableIcon::activated, this,
            &FriendsList::OnBlockUsersClicked);
    connect(ui->icon_block_users_2, &ClickableIcon::activated, this,
            &FriendsList::OnBlockUsersClicked);
    connect(ui->icon_friends_list, &ClickableIcon::activated, this,
            &FriendsList::OnFriendsListClicked);
    connect(ui->icon_friends_list_2, &ClickableIcon::activated, this,
            &FriendsList::OnFriendsListClicked);
    connect(ui->icon_arrow_back, &ClickableIcon::activated, this,
            &FriendsList::OnFriendsListClicked);
    connect(ui->icon_arrow_back_2, &ClickableIcon::activated, this,
            &FriendsList::OnFriendsListClicked);

    connect(ui->button_friend_add, &QPushButton::pressed, this, &FriendsList::OnAddFriend);
    connect(ui->line_edit_friend_add, &QLineEdit::textChanged, this,
            [this](const QString& code) { OnValidateFriendCode(ui->button_friend_add, code); });
    connect(ui->line_edit_friend_add, &QLineEdit::returnPressed, ui->button_friend_add,
            &QPushButton::pressed);

    connect(ui->button_friend_block, &QPushButton::pressed, this, &FriendsList::OnBlockUser);
    connect(ui->line_edit_friend_block, &QLineEdit::textChanged, this,
            [this](const QString& code) { OnValidateFriendCode(ui->button_friend_block, code); });
    connect(ui->line_edit_friend_block, &QLineEdit::returnPressed, ui->button_friend_block,
            &QPushButton::pressed);

    connect(friend_list_delegate, &UserDelegate::ButtonPressed, this,
            &FriendsList::OnFriendRemovePressed);
    connect(incoming_requests_delegate, &UserDelegate::ButtonPressed, this,
            &FriendsList::OnIncomingRequestPressed);
    connect(sent_requests_delegate, &UserDelegate::ButtonPressed, this,
            &FriendsList::OnSentRequestCancel);
    connect(blocked_users_delegate, &UserDelegate::ButtonPressed, this,
            &FriendsList::OnUnblockUser);

    connect(ui->label_friend_code, &ClickableIcon::activated, this, &FriendsList::OnCopyFriendCode);

    connect(&download_future, &QFutureWatcher<bool>::finished, this,
            &FriendsList::OnDownloadFinished);

    Reload();
}

FriendsList::~FriendsList() {
    std::scoped_lock lock{state_mutex};
    operation_future.waitForFinished();
    download_future.waitForFinished();
}

void FriendsList::Reload() {
    ClearWidgets();
    ClearState();
    Disable();
    download_future.waitForFinished();
    download_future.setFuture(QtConcurrent::run([this] { return Download(); }));
}

void FriendsList::Refresh() {
    download_future.waitForFinished();
    download_future.setFuture(QtConcurrent::run([this] { return Download(); }));
}

void FriendsList::OnChangedAirplaneMode() {
    Refresh();
}

void FriendsList::OnDownloadFinished() {
    if (download_future.result()) {
        RefreshState();
        Enable();
    } else {
        Disable();
    }
}

void FriendsList::OnFriendRemovePressed(int user_index, int button_index) {
    if (operation_future.isRunning()) {
        return;
    }
    QStandardItemModel* const model = friend_list_model;
    const auto info = model->index(user_index, 0).data(ROLE_INDEX).value<UserInfo*>();
    const std::string key = info->Key();

    const bool is_block = button_index == 1;
    const QString title = is_block ? tr("Block Friend") : tr("Remove Friend");
    const QString text =
        is_block ? tr("<p>Are you sure you want to block <b>%1</b>?</p>")
                 : tr("<p>Are you sure you want to remove<br/><b>%1</b> from your friends?</p>");

    if (QMessageBox::question(this, title, text.arg(info->username),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    ui->friendsListToolBox->setFocus();

    if (is_block) {
        AsyncOperation("block friend", &FriendsList::OnBlockFriendFinished, [this, key] {
            if (const int result = RemoveFriend(online_initiator, key); result != 200) {
                return result;
            }
            return BlockUser(online_initiator, key);
        });
    } else {
        AsyncOperation("remove friend", &FriendsList::OnRemoveFriendFinished,
                       [this, key] { return RemoveFriend(online_initiator, key); });
    }
}

void FriendsList::OnRemoveFriendFinished(int result) {
    if (result != 200) {
        QMessageBox::critical(this, tr("Remove Friend"), tr("Error occured while removing friend"));
    }
}

void FriendsList::OnBlockFriendFinished(int result) {
    if (result != 200) {
        QMessageBox::critical(this, tr("Block Friend"), tr("Error occured while blocking friend"));
    }
}

void FriendsList::OnAddFriend() {
    const QString code = ui->line_edit_friend_add->text();
    if (!IsFriendCodeValid(code) || operation_future.isRunning()) {
        return;
    }
    ui->line_edit_friend_add->clear();
    ui->friendsAddToolBox->setFocus();
    AsyncOperation("add friend", &FriendsList::OnAddFriendFinished,
                   [this, code] { return AddFriend(online_initiator, code.toStdString()); });
}

void FriendsList::OnAddFriendFinished(int result) {
    switch (result) {
    case 200:
        QToolTip::showText(ui->line_edit_friend_add->mapToGlobal(QPoint()),
                           tr("Sent friend request"), ui->line_edit_friend_add);
        break;
    case 400:
        QMessageBox::critical(this, tr("Send Friend Request"),
                              tr("Friend code doesn't correspond to a valid user."));
        break;
    default:
        QMessageBox::critical(this, tr("Send Friend Request"),
                              tr("Unknown error ocurred while sending friend request."));
        break;
    }
}

void FriendsList::OnIncomingRequestPressed(int user_index, int button_index) {
    if (operation_future.isRunning()) {
        return;
    }
    QStandardItemModel* const model = incoming_requests_model;
    const auto info = model->index(user_index, 0).data(ROLE_INDEX).value<UserInfo*>();
    const std::string key = info->Key();
    const bool accept = button_index == 0;
    ui->friendsAddToolBox->setFocus();
    AsyncOperation(
        "change incoming friend request", &FriendsList::OnIncomingRequestChangedFinished,
        [this, info, accept, key] { return ChangeFriendRequest(online_initiator, key, accept); });
}

void FriendsList::OnIncomingRequestChangedFinished(int result) {
    if (result != 200) {
        QMessageBox::critical(this, tr("Reply Friend Request"),
                              tr("Error occurred while replying to an incoming friend request"));
    }
}

void FriendsList::OnSentRequestCancel(int user_index, int button_index) {
    if (operation_future.isRunning()) {
        return;
    }
    QStandardItemModel* const model = sent_requests_model;
    const auto info = model->index(user_index, 0).data(ROLE_INDEX).value<UserInfo*>();
    const std::string key = info->Key();
    ui->friendsAddToolBox->setFocus();
    AsyncOperation("cancel sent request", &FriendsList::OnSentRequestCancelFinished,
                   [this, key] { return CancelSentFriendRequest(online_initiator, key); });
}

void FriendsList::OnSentRequestCancelFinished(int result) {
    if (result != 200) {
        QMessageBox::critical(this, tr("Cancel Sent Request"),
                              tr("Error occurred while trying to cancel a sent request."));
    }
}

void FriendsList::OnBlockUser() {
    const QString code = ui->line_edit_friend_block->text();
    if (!IsFriendCodeValid(code) || operation_future.isRunning()) {
        return;
    }
    ui->line_edit_friend_block->clear();
    ui->friendsBlockToolBox->setFocus();
    AsyncOperation("block user", &FriendsList::OnBlockUserFinished,
                   [this, code = code.toStdString()] { return BlockUser(online_initiator, code); });
}

void FriendsList::OnBlockUserFinished(int result) {
    switch (result) {
    case 200:
        QToolTip::showText(ui->line_edit_friend_block->mapToGlobal(QPoint()), tr("User blocked"),
                           ui->line_edit_friend_block);
        break;
    default:
        QMessageBox::critical(this, tr("Block user"),
                              tr("Error occurred while trying to block user."));
    }
}

void FriendsList::OnUnblockUser(int user_index, int button_index) {
    if (operation_future.isRunning()) {
        return;
    }
    QStandardItemModel* const model = blocked_users_model;
    const auto info = model->index(user_index, 0).data(ROLE_INDEX).value<UserInfo*>();
    const std::string key = info->Key();
    ui->friendsBlockToolBox->setFocus();
    AsyncOperation("unblock user", &FriendsList::OnUnblockUserFinished,
                   [this, key] { return UnblockUser(online_initiator, key); });
}

void FriendsList::OnUnblockUserFinished(int result) {
    if (result != 200) {
        QMessageBox::critical(this, tr("Unblock user"),
                              tr("Error occurred while trying to unblock user."));
    }
}

void FriendsList::OnCopyFriendCode() {
    QClipboard* const clipboard = QApplication::clipboard();
    QLabel* const label = ui->label_friend_code;
    clipboard->setText(label->text());
    QToolTip::showText(label->mapToGlobal(QPoint()), tr("Copied to clipboard!"), label);
}

void FriendsList::OnValidateFriendCode(QWidget* button, const QString& code) {
    button->setEnabled(IsFriendCodeValid(code));
}

void FriendsList::OnFriendsListClicked() {
    ui->friendsStackedWidget->setCurrentWidget(ui->FriendsList);
}

void FriendsList::OnFriendsAddClicked() {
    ui->friendsStackedWidget->setCurrentWidget(ui->FriendsAdd);
}

void FriendsList::OnBlockUsersClicked() {
    ui->friendsStackedWidget->setCurrentWidget(ui->FriendsBlock);
}

void FriendsList::Disable() {
    setEnabled(false);
    user_avatar_scene->clear();
    ui->userNameCode->hide();
    ui->userStatus->hide();
}

void FriendsList::Enable() {
    EnableActions(true);
    setEnabled(true);
    ui->userNameCode->show();
    ui->userStatus->show();
}

void FriendsList::ClearState() {
    std::scoped_lock lock{state_mutex};
    state = State{};
    old_state = State{};
}

void FriendsList::ClearWidgets() {
    user_avatar_scene->clear();
    friend_list_model->clear();
    sent_requests_model->clear();
    incoming_requests_model->clear();
    blocked_users_model->clear();
}

void FriendsList::EnableActions(bool state) {
    ui->button_friend_add->setEnabled(state);
    ui->button_friend_block->setEnabled(state);
    ui->line_edit_friend_add->setEnabled(state);
    ui->list_view_friends->setEnabled(state);
    ui->list_view_sent_requests->setEnabled(state);
    ui->list_view_incoming_requests->setEnabled(state);
    ui->list_view_blocked_users->setEnabled(state);
}

void FriendsList::RefreshState() {
    std::scoped_lock lock{state_mutex};
    RefreshUserState();
    RefreshFriendListState();
    RefreshSentRequests();
    RefreshIncomingRequests();
    RefreshBlockedUsers();
}

void FriendsList::RefreshUserState() {
    user_avatar_scene->clear();
    user_avatar_scene->addPixmap(state.avatar);

    ui->label_online_username->setText(QString::fromStdString(state.username));
    ui->label_friend_code->setText(tr("%1").arg(QString::fromStdString(state.friend_code)));

    QString status_icon;
    QString status_text;
    if (state.online_status) {
        status_icon = QStringLiteral("user_online");
        if (state.title_name.empty()) {
            status_text = tr("In Menu");
        } else {
            status_text = tr("Playing %1").arg(QString::fromStdString(state.title_name));
        }
    } else {
        status_icon = QStringLiteral("user_offline");
        status_text = tr("Offline");
    }

    QPixmap pixmap = QIcon::fromTheme(status_icon).pixmap(12);
    ui->label_user_status_icon->setPixmap(pixmap);
    ui->label_user_status_text->setText(status_text);
}

void FriendsList::RefreshFriendListState() {
    RefreshModel(state.friends, old_state.friends, friend_list_model,
                 [paintable = ui->list_view_friends->viewport()](const std::string& key,
                                                                 const Friend& friend_data) {
                     return new UserInfo(key, friend_data, paintable);
                 });
    ui->list_view_friends->viewport()->repaint();
}

void FriendsList::RefreshSentRequests() {
    RefreshModel(state.sent_requests, old_state.sent_requests, sent_requests_model,
                 [paintable = ui->list_view_sent_requests->viewport()](
                     const std::string& key, const FriendRequest& request) {
                     return new UserInfo(key, request, false, paintable);
                 });
}

void FriendsList::RefreshIncomingRequests() {
    RefreshModel(state.incoming_requests, old_state.incoming_requests, incoming_requests_model,
                 [paintable = ui->list_view_incoming_requests](const std::string& key,
                                                               const FriendRequest& request) {
                     return new UserInfo(key, request, true, paintable);
                 });
}

void FriendsList::RefreshBlockedUsers() {
    RefreshModel(state.blocked_users, old_state.blocked_users, blocked_users_model,
                 [paintable = ui->list_view_blocked_users->viewport()](
                     const std::string& key, const BlockedUser& blocked_user) {
                     return new UserInfo(key, blocked_user, paintable);
                 });
}

bool FriendsList::Download() noexcept try {
    if (!online_initiator.IsConnected() || Settings::values.raptor_token.empty()) {
        return false;
    }
    const httplib::Headers profile_headers = AuthorizationHeaders(online_initiator, "profile");
    const httplib::Headers headers = AuthorizationHeaders(online_initiator, "friends");

    const std::string profile_api_url = online_initiator.ProfileApiUrl();
    const std::string friends_api_url = online_initiator.FriendsApiUrl();

    const auto get = [](const std::string& host, const httplib::Headers& headers,
                        const char* path) {
        httplib::SSLClient client(host);
        client.set_follow_location(true);
        return std::make_shared<httplib::Result>(client.Get(path, headers));
    };
    auto online_status = std::async(get, friends_api_url, headers, "/api/v1/me/online_status");
    auto username = std::async(get, profile_api_url, profile_headers, "/api/v1/username");
    auto avatar = std::async(get, profile_api_url, profile_headers, "/api/v1/avatar/64/64");
    auto friend_code = std::async(get, friends_api_url, headers, "/api/v1/me/friend_code");
    auto friends = std::async(get, friends_api_url, headers, "/api/v1/me/friends");
    auto sent_requests = std::async(get, friends_api_url, headers, "/api/v1/request/out");
    auto incoming_requests = std::async(get, friends_api_url, headers, "/api/v1/request/in");
    auto blocked_users = std::async(get, friends_api_url, headers, "/api/v1/block");

    auto online_status_value = online_status.get();
    State new_state{
        .username = GetUsername(*(username.get())),
        .avatar = GetAvatar(*(avatar.get())),
        .friend_code = GetFriendCode(*(friend_code.get())),
        .online_status = GetOnlineStatus(*online_status_value),
        .title_name = GetCurrentTitleName(*online_status_value),
        .friends = GetFriendList(*(friends.get())),
        .sent_requests = GetRequests(*(sent_requests.get())),
        .incoming_requests = GetRequests(*(incoming_requests.get())),
        .blocked_users = GetBlockedUsers(*(blocked_users.get())),
    };
    std::scoped_lock lock{state_mutex};
    old_state = std::exchange(state, new_state);
    return true;

} catch (const std::exception& e) {
    LOG_ERROR(Frontend, "Friend list process error: {}", e.what());
    return false;
}

// See declaration (tag: C++20)
// template <typename QtFunc, std::invocable<> AsyncFunc>
template <typename QtFunc, typename AsyncFunc>
void FriendsList::AsyncOperation(std::string_view context, QtFunc&& qt_func,
                                 AsyncFunc&& async_func) {
    EnableActions(false);

    operation_future.disconnect(operation_connection);
    operation_connection =
        connect(&operation_future, &QFutureWatcher<int>::finished, [this, qt_func] {
            (this->*qt_func)(operation_future.result());
            Refresh();
        });

    operation_future.setFuture(QtConcurrent::run([this, async_func, context] {
        try {
            return async_func();
        } catch (const std::exception& e) {
            LOG_ERROR(Frontend, "Error processing {}: {}", context, e.what());
            return -1;
        }
    }));
}
