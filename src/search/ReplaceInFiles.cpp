#include "search/ReplaceInFiles.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace tf {

namespace fs = std::filesystem;

bool matchStillAt(std::string_view line, const SearchMatch& m) {
  if (m.column < 0 || m.length <= 0 || m.textOffset < 0) return false;
  const size_t off = static_cast<size_t>(m.textOffset), col = static_cast<size_t>(m.column),
               len = static_cast<size_t>(m.length);
  if (col < off || col + len > off + m.text.size()) return false;  // the match lies inside the text the search kept
  if (off + m.text.size() > line.size()) return false;
  return line.compare(off, m.text.size(), m.text) == 0;
}

std::string replaceMatchesInText(std::string_view text, std::vector<SearchMatch> matches,
                                 const std::string& replacement, ReplaceCounts* counts) {
  std::sort(matches.begin(), matches.end(), [](const SearchMatch& a, const SearchMatch& b) {
    return a.line != b.line ? a.line < b.line : a.column < b.column;
  });
  // Line start offsets; a line ends before its '\n' (and a '\r' in front of it).
  std::vector<size_t> starts{0};
  for (size_t i = 0; i < text.size(); ++i)
    if (text[i] == '\n') starts.push_back(i + 1);
  auto lineView = [&](int l) {
    const size_t b = starts[static_cast<size_t>(l)];
    size_t e = static_cast<size_t>(l) + 1 < starts.size() ? starts[static_cast<size_t>(l) + 1] - 1 : text.size();
    if (e > b && text[e - 1] == '\r') --e;
    return text.substr(b, e - b);
  };
  ReplaceCounts local;
  std::string out;
  out.reserve(text.size());
  size_t done = 0;  // text[0, done) is in `out`
  for (const SearchMatch& m : matches) {
    if (m.line < 0 || static_cast<size_t>(m.line) >= starts.size() || !matchStillAt(lineView(m.line), m)) {
      ++local.stale;
      continue;
    }
    const size_t at = starts[static_cast<size_t>(m.line)] + static_cast<size_t>(m.column);
    if (at < done) {  // overlaps the previous replacement
      ++local.stale;
      continue;
    }
    out.append(text.substr(done, at - done));
    out += replacement;
    done = at + static_cast<size_t>(m.length);
    ++local.replaced;
  }
  out.append(text.substr(done));
  if (counts) {
    counts->replaced += local.replaced;
    counts->stale += local.stale;
  }
  return out;
}

bool replaceMatchesInFile(const fs::path& file, const std::vector<SearchMatch>& matches, const std::string& replacement,
                          ReplaceCounts* counts, std::string* error) {
  auto fail = [&](const std::string& m) {
    if (error) *error = m;
    return false;
  };
  std::string text;
  {
    std::ifstream in(file, std::ios::binary);
    if (!in) return fail("cannot read " + file.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    text = ss.str();
  }
  ReplaceCounts local;
  const std::string out = replaceMatchesInText(text, matches, replacement, &local);
  if (counts) {
    counts->replaced += local.replaced;
    counts->stale += local.stale;
  }
  if (local.replaced == 0) return true;
  fs::path tmp = file;
  tmp += ".tenfold-replace";
  std::error_code ec;
  {
    std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
    if (!o) return fail("cannot write " + file.string());
    o.write(out.data(), static_cast<std::streamsize>(out.size()));
    o.flush();
    if (!o) {
      o.close();
      fs::remove(tmp, ec);
      return fail("cannot write " + file.string());
    }
  }
  fs::permissions(tmp, fs::status(file, ec).permissions(), ec);
  ec.clear();
  fs::rename(tmp, file, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return fail("cannot replace " + file.string() + ": " + ec.message());
  }
  return true;
}

}  // namespace tf
