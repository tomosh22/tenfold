#include "semantic/CppLexer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <unordered_set>

namespace tf {

bool CppLexer::isKeyword(std::string_view s) {
  static const std::unordered_set<std::string_view> kw = {"alignas",
                                                          "alignof",
                                                          "and",
                                                          "asm",
                                                          "auto",
                                                          "bool",
                                                          "break",
                                                          "case",
                                                          "catch",
                                                          "char",
                                                          "char8_t",
                                                          "char16_t",
                                                          "char32_t",
                                                          "class",
                                                          "concept",
                                                          "const",
                                                          "consteval",
                                                          "constexpr",
                                                          "constinit",
                                                          "const_cast",
                                                          "continue",
                                                          "co_await",
                                                          "co_return",
                                                          "co_yield",
                                                          "decltype",
                                                          "default",
                                                          "delete",
                                                          "do",
                                                          "double",
                                                          "dynamic_cast",
                                                          "else",
                                                          "enum",
                                                          "explicit",
                                                          "export",
                                                          "extern",
                                                          "false",
                                                          "float",
                                                          "for",
                                                          "friend",
                                                          "goto",
                                                          "if",
                                                          "inline",
                                                          "int",
                                                          "long",
                                                          "mutable",
                                                          "namespace",
                                                          "new",
                                                          "noexcept",
                                                          "not",
                                                          "nullptr",
                                                          "operator",
                                                          "or",
                                                          "private",
                                                          "protected",
                                                          "public",
                                                          "register",
                                                          "reinterpret_cast",
                                                          "requires",
                                                          "return",
                                                          "short",
                                                          "signed",
                                                          "sizeof",
                                                          "static",
                                                          "static_assert",
                                                          "static_cast",
                                                          "struct",
                                                          "switch",
                                                          "template",
                                                          "this",
                                                          "thread_local",
                                                          "throw",
                                                          "true",
                                                          "try",
                                                          "typedef",
                                                          "typeid",
                                                          "typename",
                                                          "union",
                                                          "unsigned",
                                                          "using",
                                                          "virtual",
                                                          "void",
                                                          "volatile",
                                                          "wchar_t",
                                                          "while",
                                                          "override",
                                                          "final",
                                                          "__declspec",
                                                          "__attribute__",
                                                          "__forceinline",
                                                          "__inline",
                                                          "__cdecl",
                                                          "__stdcall",
                                                          "__fastcall",
                                                          "__vectorcall",
                                                          "__restrict",
                                                          "__pragma",
                                                          "_Pragma",
                                                          "__int64",
                                                          "__int32",
                                                          "__int16",
                                                          "__int8",
                                                          "__asm",
                                                          "__try",
                                                          "__except",
                                                          "__finally",
                                                          "__leave",
                                                          "__uuidof",
                                                          "__alignof",
                                                          "__builtin_offsetof"};
  return kw.count(s) > 0;
}

bool CppLexer::isCKeyword(std::string_view s) {
  static const std::unordered_set<std::string_view> kw = {"alignas",
                                                          "alignof",
                                                          "asm",
                                                          "auto",
                                                          "bool",
                                                          "break",
                                                          "case",
                                                          "char",
                                                          "const",
                                                          "constexpr",
                                                          "continue",
                                                          "default",
                                                          "do",
                                                          "double",
                                                          "else",
                                                          "enum",
                                                          "extern",
                                                          "false",
                                                          "float",
                                                          "for",
                                                          "goto",
                                                          "if",
                                                          "inline",
                                                          "int",
                                                          "long",
                                                          "nullptr",
                                                          "register",
                                                          "restrict",
                                                          "return",
                                                          "short",
                                                          "signed",
                                                          "sizeof",
                                                          "static",
                                                          "static_assert",
                                                          "struct",
                                                          "switch",
                                                          "thread_local",
                                                          "true",
                                                          "typedef",
                                                          "typeof",
                                                          "union",
                                                          "unsigned",
                                                          "void",
                                                          "volatile",
                                                          "while",
                                                          "_Alignas",
                                                          "_Alignof",
                                                          "_Atomic",
                                                          "_Bool",
                                                          "_Complex",
                                                          "_Generic",
                                                          "_Imaginary",
                                                          "_Noreturn",
                                                          "_Static_assert",
                                                          "_Thread_local",
                                                          "_Pragma",
                                                          "__declspec",
                                                          "__attribute__",
                                                          "__forceinline",
                                                          "__inline",
                                                          "__cdecl",
                                                          "__stdcall",
                                                          "__fastcall",
                                                          "__vectorcall",
                                                          "__restrict",
                                                          "__pragma",
                                                          "__int64",
                                                          "__int32",
                                                          "__int16",
                                                          "__int8",
                                                          "__asm",
                                                          "__try",
                                                          "__except",
                                                          "__finally",
                                                          "__leave",
                                                          "__uuidof",
                                                          "__alignof",
                                                          "__builtin_offsetof"};
  return kw.count(s) > 0;
}

bool CppLexer::nlAt(size_t k) const {
  const char c = s_[k];
  return c == '\n' || (c == '\r' && (k + 1 >= s_.size() || s_[k + 1] != '\n'));
}

size_t CppLexer::contLen(size_t k) const {
  if (s_[k] != '\\' || k + 1 >= s_.size()) return 0;
  if (s_[k + 1] == '\n') return 2;
  if (s_[k + 1] == '\r') return (k + 2 < s_.size() && s_[k + 2] == '\n') ? 3 : 2;
  return 0;
}

Token CppLexer::make(Tok k, size_t start, int line, int col) const {
  return Token{k, s_.substr(start, i_ - start), line, col};
}

void CppLexer::skipSpaceAndComments() {
  while (i_ < s_.size()) {
    char c = s_[i_];
    if (nlAt(i_)) {
      ++i_;
      ++line_;
      lineStart_ = i_;
      atLineStart_ = true;
    } else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
      ++i_;
    } else if (size_t n = contLen(i_)) {
      // line continuation outside a directive
      i_ += n;
      ++line_;
      lineStart_ = i_;
    } else if (c == '/' && i_ + 1 < s_.size() && s_[i_ + 1] == '/') {
      while (i_ < s_.size() && !nlAt(i_)) {
        if (size_t n = contLen(i_)) {
          ++line_;
          i_ += n;
          lineStart_ = i_;
          continue;
        }
        ++i_;
      }
    } else if (c == '/' && i_ + 1 < s_.size() && s_[i_ + 1] == '*') {
      i_ += 2;
      while (i_ < s_.size() && !(s_[i_] == '*' && i_ + 1 < s_.size() && s_[i_ + 1] == '/')) {
        if (nlAt(i_)) {
          ++line_;
          lineStart_ = i_ + 1;
        }
        ++i_;
      }
      i_ = std::min(i_ + 2, s_.size());
    } else {
      break;
    }
  }
}

// (Line, column) of offset `off` in the editor's lines: clang's line `clangLine` (starting at `clangLineStart`) plus
// the NEL / LS / PS before `off`.
std::pair<int, int> CppLexer::place(size_t off, int clangLine, size_t clangLineStart) {
  while (sepIdx_ < seps_.size() && seps_[sepIdx_].first < off) ++sepIdx_;
  size_t from = clangLineStart;
  if (sepIdx_ > 0) from = std::max(from, seps_[sepIdx_ - 1].first + seps_[sepIdx_ - 1].second);
  return {clangLine + static_cast<int>(sepIdx_), static_cast<int>(off - from)};
}

Token CppLexer::next() {
  Token t = nextRaw();
  if (seps_.empty()) return t;  // no NEL / LS / PS in the text (nearly always): clang's lines are the editor's
  const size_t off = t.kind == Tok::End ? i_ : static_cast<size_t>(t.text.data() - s_.data());
  const auto [line, col] = place(off, t.line, off - static_cast<size_t>(t.col));
  t.line = line;
  t.col = col;
  return t;
}

Token CppLexer::nextRaw() {
  skipSpaceAndComments();
  if (i_ >= s_.size()) return Token{Tok::End, {}, line_, static_cast<int>(i_ - lineStart_)};
  const size_t start = i_;
  const int line = line_;
  const int col = static_cast<int>(i_ - lineStart_);
  const char c = s_[i_];
  auto peek = [&](size_t k) -> char { return i_ + k < s_.size() ? s_[i_ + k] : '\0'; };

  // Preprocessor directive: '#' as first non-space char on the line.
  if (c == '#' && atLineStart_) {
    while (i_ < s_.size() && !nlAt(i_)) {
      if (size_t n = contLen(i_)) {
        i_ += n;
        ++line_;
        lineStart_ = i_;
        continue;
      }
      if (s_[i_] == '/' && peek(1) == '*') {  // block comment inside directive
        i_ += 2;
        while (i_ < s_.size() && !(s_[i_] == '*' && peek(1) == '/')) {
          if (nlAt(i_)) {
            ++line_;
            lineStart_ = i_ + 1;
          }
          ++i_;
        }
        i_ = std::min(i_ + 2, s_.size());
        continue;
      }
      if (s_[i_] == '/' && peek(1) == '/') {
        while (i_ < s_.size() && !nlAt(i_)) ++i_;
        break;
      }
      ++i_;
    }
    return make(Tok::Preprocessor, start, line, col);
  }
  atLineStart_ = false;

  // Identifier / keyword / prefixed string.
  if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$' || static_cast<unsigned char>(c) >= 0x80) {
    // string / char prefixes: u8 u U L and raw R
    size_t j = i_;
    while (j < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[j])) || s_[j] == '_' || s_[j] == '$' ||
                             static_cast<unsigned char>(s_[j]) >= 0x80))
      ++j;
    std::string_view word = s_.substr(i_, j - i_);
    char q = j < s_.size() ? s_[j] : '\0';
    bool isPrefix = (word == "u8" || word == "u" || word == "U" || word == "L" || word == "R" || word == "u8R" ||
                     word == "uR" || word == "UR" || word == "LR");
    if (isPrefix && (q == '"' || (q == '\'' && word.back() != 'R'))) {
      i_ = j;  // fall through to string/char handling below with prefix included
      bool raw = word.back() == 'R';
      if (raw) {
        ++i_;  // opening quote
        size_t dstart = i_;
        while (i_ < s_.size() && s_[i_] != '(') ++i_;
        std::string delim = ")" + std::string(s_.substr(dstart, i_ - dstart)) + "\"";
        size_t end = s_.find(delim, i_);
        if (end == std::string_view::npos) {
          i_ = s_.size();
        } else {
          for (size_t k = i_; k < end; ++k)
            if (nlAt(k)) {
              ++line_;
              lineStart_ = k + 1;
            }
          i_ = end + delim.size();
        }
        return make(Tok::String, start, line, col);
      }
      char quote = q;
      ++i_;
      while (i_ < s_.size() && s_[i_] != quote && !nlAt(i_)) {
        if (s_[i_] == '\\') ++i_;
        ++i_;
      }
      if (i_ < s_.size() && s_[i_] == quote) ++i_;
      return make(quote == '"' ? Tok::String : Tok::Char, start, line, col);
    }
    i_ = j;
    return make((isC_ ? isCKeyword(word) : isKeyword(word)) ? Tok::Keyword : Tok::Identifier, start, line, col);
  }

  // Number.
  if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1))))) {
    ++i_;
    while (i_ < s_.size()) {
      char d = s_[i_];
      if (std::isalnum(static_cast<unsigned char>(d)) || d == '_' || d == '.' || d == '\'') {
        ++i_;
        continue;
      }
      if ((d == '+' || d == '-') &&
          (s_[i_ - 1] == 'e' || s_[i_ - 1] == 'E' || s_[i_ - 1] == 'p' || s_[i_ - 1] == 'P')) {
        ++i_;
        continue;
      }
      break;
    }
    return make(Tok::Number, start, line, col);
  }

  // String / char.
  if (c == '"' || c == '\'') {
    ++i_;
    while (i_ < s_.size() && s_[i_] != c && !nlAt(i_)) {
      if (s_[i_] == '\\') ++i_;
      ++i_;
    }
    if (i_ < s_.size() && s_[i_] == c) ++i_;
    return make(c == '"' ? Tok::String : Tok::Char, start, line, col);
  }

  // Punctuators: longest match.
  static const std::array<std::string_view, 24> three = {"...", "<<=", ">>=", "->*", "<=>"};
  static const std::array<std::string_view, 24> two = {
      "::", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&",
      "||", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "##", ".*"};
  for (auto p : three) {
    if (!p.empty() && s_.substr(i_, 3) == p) {
      i_ += 3;
      return make(Tok::Punct, start, line, col);
    }
  }
  for (auto p : two) {
    if (!p.empty() && s_.substr(i_, 2) == p) {
      i_ += 2;
      return make(Tok::Punct, start, line, col);
    }
  }
  ++i_;
  return make(Tok::Punct, start, line, col);
}

std::vector<Token> CppLexer::all() {
  std::vector<Token> out;
  out.reserve(s_.size() / 4 + 16);
  while (true) {
    Token t = next();
    out.push_back(t);
    if (t.kind == Tok::End) break;
  }
  return out;
}

}  // namespace tf
