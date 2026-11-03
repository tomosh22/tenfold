#pragma once
#include <string>

#include <nlohmann/json.hpp>

namespace tf {

// json.dump() with the default error handler throws type_error 316 on invalid UTF-8, which would terminate the
// editor when user text (a path, a file name, a bookmark label, a document sent to a language server) holds such bytes.
// Every dump() of data that can carry user text goes through this: invalid bytes become U+FFFD.
inline std::string dumpSafe(const nlohmann::json& j, int indent = -1) {
  return j.dump(indent, ' ', false, nlohmann::json::error_handler_t::replace);
}

}  // namespace tf
