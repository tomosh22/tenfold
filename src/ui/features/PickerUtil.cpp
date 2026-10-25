#include "ui/features/PickerUtil.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace tf::ui {

namespace fs = std::filesystem;

bool LineSource::lines(const fs::path& file, int from, int count, std::vector<std::string>& out, int& firstLine) {
  out.clear();
  const std::vector<std::string>* src = nullptr;
  if (Document* d = ed_.document(file)) {
    const int n = d->buffer().lineCount();
    from = std::clamp(from, 0, std::max(0, n - 1));
    for (int i = from; i < n && i < from + count; ++i) out.push_back(d->buffer().line(i));
    firstLine = from;
    return true;
  }
  const std::string key = file.generic_string();
  auto it = cache_.find(key);
  if (it == cache_.end()) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    std::string text(std::min<size_t>(static_cast<size_t>(std::max<std::streamoff>(0, in.tellg())), 4u << 20), '\0');
    in.seekg(0);
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(in.gcount()));
    if (cache_.size() >= kMaxFiles) cache_.clear();
    std::vector<std::string> ls;
    std::istringstream ss(text);
    for (std::string l; std::getline(ss, l);) {
      if (!l.empty() && l.back() == '\r') l.pop_back();
      ls.push_back(std::move(l));
    }
    it = cache_.emplace(key, std::move(ls)).first;
  }
  src = &it->second;
  const int n = static_cast<int>(src->size());
  from = std::clamp(from, 0, std::max(0, n - 1));
  for (int i = from; i < n && i < from + count; ++i) out.push_back((*src)[static_cast<size_t>(i)]);
  firstLine = from;
  return true;
}

std::string LineSource::line(const fs::path& file, int n) {
  std::vector<std::string> v;
  int first = 0;
  if (!lines(file, n, 1, v, first) || v.empty() || first != n) return {};
  return v[0];
}

std::string relativePath(const fs::path& root, const fs::path& p) {
  const std::string r = root.generic_string(), s = p.generic_string();
  if (s.size() > r.size() + 1 && s.compare(0, r.size(), r) == 0 && s[r.size()] == '/') return s.substr(r.size() + 1);
  return s;
}

void kindIcon(const std::string& kind, std::string& icon, std::string& token) {
  struct K {
    const char *kind, *icon, *token;
  };
  static const K table[] = {{"function", "\xC6\x92", "function"},  // U+0192
                            {"method", "\xC6\x92", "method"},
                            {"constructor", "\xC6\x92", "function"},
                            {"operator", "\xC6\x92", "function"},
                            {"class", "C", "class"},
                            {"struct", "S", "struct"},
                            {"union", "U", "struct"},
                            {"interface", "I", "interface"},
                            {"enum", "E", "enum"},
                            {"enumerator", "e", "enumMember"},
                            {"namespace", "N", "namespace"},
                            {"module", "N", "namespace"},
                            {"variable", "v", "variable"},
                            {"constant", "v", "variable"},
                            {"field", "f", "property"},
                            {"property", "f", "property"},
                            {"typedef", "T", "type"},
                            {"type parameter", "T", "typeParameter"},
                            {"macro", "#", "macro"},
                            {"concept", "K", "concept"}};
  for (const K& k : table)
    if (kind == k.kind) {
      icon = k.icon;
      token = k.token;
      return;
    }
  icon = "?";
  token.clear();
}

std::string trimmedLine(std::string s, size_t max) {
  for (char& c : s)
    if (c == '\t') c = ' ';
  const size_t a = s.find_first_not_of(' ');
  s = a == std::string::npos ? std::string() : s.substr(a);
  while (!s.empty() && s.back() == ' ') s.pop_back();
  if (s.size() > max) {
    size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    s.resize(cut);
  }
  return s;
}

std::string locationKey(const SymbolLocation& l) {
  return l.loc.file.generic_string() + ":" + std::to_string(l.loc.pos.line) + ":" + std::to_string(l.loc.pos.column) +
         ":" + l.name;
}

std::string confidenceBadge(Confidence c, const std::string& err) {
  if (c == Confidence::Authoritative) return err;
  return err.empty() ? "verifying\xE2\x80\xA6" : "approximate (" + err + ")";
}

}  // namespace tf::ui
