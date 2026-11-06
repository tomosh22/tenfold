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
  // Line start offsets; a line ends before its '\n' (and a '\r' in front of it) or at a lone '\r'.
  std::vector<size_t> starts{0};
  for (size_t i = 0; i < text.size(); ++i)
    if (text[i] == '\n' || (text[i] == '\r' && !(i + 1 < text.size() && text[i + 1] == '\n'))) starts.push_back(i + 1);
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
    local.appliedAt.emplace_back(m.line, m.column);
    done = at + static_cast<size_t>(m.length);
    ++local.replaced;
  }
  out.append(text.substr(done));
  if (counts) {
    counts->replaced += local.replaced;
    counts->stale += local.stale;
    counts->appliedAt.insert(counts->appliedAt.end(), local.appliedAt.begin(), local.appliedAt.end());
  }
  return out;
}

bool replaceMatchesInFile(const fs::path& file, const std::vector<SearchMatch>& matches, const std::string& replacement,
                          ReplaceCounts* counts, std::string* error, UnencodableReport* refused) {
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
  const Encoding enc = isValidUtf8(text) ? Encoding::Utf8 : decodeFileBytes(text).encoding;
  std::string rep = replacement;  // in the file's own encoding
  if (enc == Encoding::Windows1252) {
    if (!encodeText(replacement, enc, rep)) {
      // Find out where it would have gone (the matches that are still valid), then leave the file alone.
      ReplaceCounts probe;
      replaceMatchesInText(text, matches, std::string(), &probe);
      if (counts) counts->stale += probe.stale;
      if (probe.replaced == 0) return true;  // nothing to replace, so nothing to refuse
      UnencodableReport bad;
      bad.encoding = enc;
      std::vector<size_t> starts{0};  // line starts, by replaceMatchesInText's rule (CRLF, LF, lone CR)
      for (size_t i = 0; i < text.size(); ++i)
        if (text[i] == '\n' || (text[i] == '\r' && !(i + 1 < text.size() && text[i + 1] == '\n')))
          starts.push_back(i + 1);
      for (const auto& [line, column] : probe.appliedAt) {
        // Matches are raw byte columns; in Windows-1252 every byte is one character, so decode the line start.
        const size_t from = starts[static_cast<size_t>(line)];
        const int col =
            static_cast<int>(decodeAs(std::string_view(text).substr(from, static_cast<size_t>(column)), enc).size());
        UnencodableReport one;
        one.encoding = enc;
        collectUnencodable(replacement, line, one);
        for (UnencodableChar c : one.chars) {
          c.column = col;
          ++bad.total;
          if (bad.chars.size() < kMaxUnencodableListed) bad.chars.push_back(std::move(c));
        }
        bad.total += one.total - one.chars.size();
      }
      if (refused) *refused = bad;
      return fail(file.filename().string() + " was not changed: " + describeUnencodable(bad) +
                  " (the replacement text)");
    }
  } else if (enc != Encoding::Utf8) {
    return fail(file.filename().string() + " was not changed: it is " + encodingName(enc) +
                ", which find in files does not read");
  }
  ReplaceCounts local;
  const std::string out = replaceMatchesInText(text, matches, rep, &local);
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
