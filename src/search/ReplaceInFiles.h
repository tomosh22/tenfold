#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/TextEncoding.h"
#include "search/TextSearch.h"

namespace tf {

// Applying find-in-files results (ReplaceInFiles). Standalone like TextSearch: it knows nothing about Editor. A
// SearchMatch is a (line, byte column, byte length) plus the text of its line as the search saw it, so a match whose
// line has changed since the search is detected and left alone instead of corrupting the file ("stale").
struct ReplaceCounts {
  size_t replaced = 0;
  size_t stale = 0;  // matches skipped because the text is no longer what the search found
  // Where the replaced matches began (line, byte column of the file as it was). Filled by replaceMatchesInText.
  std::vector<std::pair<int, int>> appliedAt;
};

// True when `line` (without its line ending) still contains, at the match's position, the text the search saw.
bool matchStillAt(std::string_view line, const SearchMatch& m);

// `text` with the matches of ONE file replaced by `replacement` (literal; regex group references are not expanded).
// Matches may come in any order; overlapping ones and stale ones are skipped. Line endings are kept as they are.
std::string replaceMatchesInText(std::string_view text, std::vector<SearchMatch> matches,
                                 const std::string& replacement, ReplaceCounts* counts = nullptr);

// Reads `file`, applies the matches and writes it back (a temporary file in the same directory, permissions copied,
// then renamed over it) when anything changed. False with `error` when the file cannot be read or written.
// The search reads raw bytes, so the matches of a Windows-1252 file are byte columns of that file: the replacement
// (UTF-8) is converted to Windows-1252 for it. If the replacement holds a character that code page cannot hold, the
// file is NOT written (false; `error` says why; `refused`, when given, lists the replacement's characters, each
// placed at the match where it would have gone, as Positions of the file decoded to UTF-8). A UTF-16 file is not
// written either (its bytes are not the text the search saw). `counts->replaced` stays 0 in both cases.
bool replaceMatchesInFile(const std::filesystem::path& file, const std::vector<SearchMatch>& matches,
                          const std::string& replacement, ReplaceCounts* counts = nullptr, std::string* error = nullptr,
                          UnencodableReport* refused = nullptr);

}  // namespace tf
