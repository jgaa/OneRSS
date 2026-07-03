#pragma once

#include <QString>

#include <optional>

namespace onerss::desktop {

[[nodiscard]] std::optional<QString> tryResolveFeedTitle(const QString &feed_url);

}  // namespace onerss::desktop
