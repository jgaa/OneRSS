#pragma once

#include "onerss.pb.h"

#include <string>
#include <vector>

namespace onerss::backend {

struct ArticleRecord {
  std::string article_id;
  std::string user_id;
  std::string node_id;
  std::string guid;
  std::string title;
  std::string link_url;
  std::string published_at;
  std::string author;
  std::string content;
  bool is_read = false;
};

onerss::pb::Article toProto(const ArticleRecord &record);

}  // namespace onerss::backend
