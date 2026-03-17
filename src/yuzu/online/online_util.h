// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include <string_view>

#include <QImage>

[[nodiscard]] QImage DownloadImageUrl(const std::string& url);

[[nodiscard]] std::string AvatarUrl(std::string url, std::string_view size);
