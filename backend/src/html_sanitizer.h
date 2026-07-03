#pragma once

#include <string>

namespace onerss::backend {

[[nodiscard]] std::string sanitizePreviewHtml(const std::string &input);

}  // namespace onerss::backend
