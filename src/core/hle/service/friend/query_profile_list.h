// Copyright 2020 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>
#include <vector>

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/thread.h"
#include "core/hle/result.h"

namespace Core {
class OnlineInitiator;
}

namespace Kernel {
class HLERequestContext;
class KEvent;
} // namespace Kernel

namespace Service::Friend {

class IFriendService;

struct Profile {
    u64 account_id;
    std::array<u8, 33> name;
    INSERT_PADDING_BYTES(7);
    std::array<u8, 160> profile_image_url;
    u8 is_valid_profile;
    INSERT_PADDING_BYTES(47);
};
static_assert(sizeof(Profile) == 256);

using ProfileList = std::vector<Profile>;

struct GetProfileListWork {
    void Execute(IFriendService*);

    void Response(Kernel::HLERequestContext&);

    // Input
    const Core::OnlineInitiator* online_initiator;
    u64 title_id;
    std::vector<u64> account_id_list;

    // Output
    std::shared_ptr<Kernel::KEvent> event;
    ProfileList profile_list;
    ResultCode result_code = RESULT_SUCCESS;
};

struct GetFriendsListWork {
    void Execute(IFriendService*);

    void Response(Kernel::HLERequestContext&);

    // Input
    const Core::OnlineInitiator* online_initiator = nullptr;
    // TODO: Add filter options

    // Output
    std::shared_ptr<Kernel::KEvent> event;
    std::vector<u64> account_ids;
    ResultCode result_code = RESULT_SUCCESS;
};

struct GetBlockedUsersWork {
    void Execute(IFriendService*);

    void Response(Kernel::HLERequestContext&);

    // Input
    const Core::OnlineInitiator* online_initiator = nullptr;

    // Output
    std::vector<u64> account_ids;
    ResultCode result_code = RESULT_SUCCESS;
};

} // namespace Service::Friend
