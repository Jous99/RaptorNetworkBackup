// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <string>
#include <string_view>

#include <httplib.h>

#include "common/logging/log.h"
#include "yuzu/online/online_util.h"

QImage DownloadImageUrl(const std::string& url) {
    static constexpr std::string_view HTTPS_PREFIX = "https://";

    if (!url.starts_with(HTTPS_PREFIX)) {
        LOG_ERROR(Frontend, "Location is not https");
        return {};
    }
    const size_t path_pos = url.find_first_of('/', HTTPS_PREFIX.size());
    const std::string host = url.substr(HTTPS_PREFIX.size(), path_pos - HTTPS_PREFIX.size());
    const std::string path = url.substr(path_pos);

    httplib::SSLClient client(host);
    auto response = client.Get(path.c_str());
    if (!response || response->status != 200) {
        LOG_ERROR(Frontend, "Failed to querying profile image");
        return {};
    }

    return QImage::fromData(reinterpret_cast<const uchar*>(response->body.data()),
                            static_cast<int>(response->body.size()));
}

std::string AvatarUrl(std::string url, std::string_view size) {
    for (size_t iteration = 0; iteration < 2; ++iteration) {
        const auto pos = url.find_last_of('%');
        if (pos != std::string::npos) {
            url.replace(pos, 1, size);
        }
    }
    return url;
}
