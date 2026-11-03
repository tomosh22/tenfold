#include "ui/features/ParserCommands.h"

#include <vector>

namespace tf::ui {

std::optional<Range> enclosingBraceScope(const TextBuffer& b, Position caret) {
  caret = b.clamp(caret);
  std::vector<Position> open;  // positions of the unmatched `{`
  bool have = false;           // the caret was passed
  size_t depth = 0;            // open.size() at the caret
  Position scopeOpen{0, 0};
  auto pass = [&](int l, int i) {
    if (have || Position{l, i} < caret) return;
    have = true;
    depth = open.size();
    if (depth) scopeOpen = open.back();
  };
  bool block = false;  // inside /* */
  for (int l = 0; l < b.lineCount(); ++l) {
    const std::string& s = b.line(l);
    const int n = static_cast<int>(s.size());
    const size_t first = s.find_first_not_of(" \t");
    const bool preproc = !block && first != std::string::npos && s[first] == '#';
    char quote = 0;
    for (int i = 0; i < n; ++i) {
      pass(l, i);
      const char c = s[static_cast<size_t>(i)];
      if (block) {
        if (c == '*' && i + 1 < n && s[static_cast<size_t>(i + 1)] == '/') {
          block = false;
          ++i;
        }
        continue;
      }
      if (quote) {
        if (c == '\\') ++i;
        else if (c == quote) quote = 0;
        continue;
      }
      if (preproc) break;
      if (c == '/' && i + 1 < n && s[static_cast<size_t>(i + 1)] == '/') break;
      if (c == '/' && i + 1 < n && s[static_cast<size_t>(i + 1)] == '*') {
        block = true;
        ++i;
        continue;
      }
      if (c == '"' || c == '\'') {
        quote = c;
        continue;
      }
      if (c == '{') {
        open.push_back({l, i});
      } else if (c == '}' && !open.empty()) {
        open.pop_back();
        if (have && depth > 0 && open.size() < depth) return Range{{scopeOpen.line, 0}, {l, n}};
      }
    }
    pass(l, n);
    if (have && depth == 0) return std::nullopt;  // the caret is outside every block
  }
  return std::nullopt;
}

Range formatScopeRange(const TextBuffer& b, Position caret) {
  caret = b.clamp(caret);
  if (auto r = enclosingBraceScope(b, caret)) return *r;
  return Range{{caret.line, 0}, {caret.line, static_cast<int>(b.line(caret.line).size())}};
}

bool isFunctionDefinition(const SymbolLocation& s, const TextBuffer& b) {
  const std::string& k = s.kind;
  if (k != "function" && k != "method" && k != "constructor" && k != "operator") return false;
  if (s.confidence == Confidence::Approximate) return s.definition;
  if (!(s.extent.start < s.extent.end)) return s.definition;
  const std::string text = b.textInRange({b.clamp(s.extent.start), b.clamp(s.extent.end)});
  int paren = 0;
  for (char c : text) {
    if (c == '(') ++paren;
    else if (c == ')') --paren;
    else if (paren <= 0 && c == '{') return true;
    else if (paren <= 0 && c == ';') return false;
  }
  return false;
}

}  // namespace tf::ui
