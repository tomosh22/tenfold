#pragma once
#include <string_view>
#include <vector>

namespace tf {

enum class Tok { Identifier, Keyword, Number, String, Char, Punct, Preprocessor, End };

struct Token {
  Tok kind = Tok::End;
  std::string_view text;
  int line = 0;  // 0-based
  int col = 0;   // 0-based byte column
  bool is(std::string_view s) const { return text == s; }
};

// A fast, forgiving C/C++ tokenizer. Comments are dropped, preprocessor
// directives are returned as a single token spanning the logical line
// (continuations included). Raw strings, prefixes and digit separators are
// handled. It never fails: unknown bytes become single-char Punct tokens. A line ends at "\r\n", "\n" or a lone '\r'
// (as in clang); a leading UTF-8 BOM is skipped (columns on line 1 still count its three bytes, like clangd's). With
// `isC` the C++-only keywords (class, delete, new, template, this, namespace, ...) are identifiers.
class CppLexer {
 public:
  explicit CppLexer(std::string_view src, bool isC = false) : s_(src), isC_(isC) {
    if (s_.size() >= 3 && s_.substr(0, 3) == "\xEF\xBB\xBF") i_ = 3;
  }
  Token next();
  std::vector<Token> all();
  static bool isKeyword(std::string_view s);   // C++
  static bool isCKeyword(std::string_view s);  // C (C11 plus the C23 spellings and the MSVC/GNU extensions)

 private:
  void skipSpaceAndComments();
  bool nlAt(size_t k) const;       // a line break ends at k ('\n', or '\r' not followed by '\n')
  size_t contLen(size_t k) const;  // length of a backslash-newline at k, else 0
  Token make(Tok k, size_t start, int line, int col) const;

  std::string_view s_;
  bool isC_ = false;
  size_t i_ = 0;
  int line_ = 0;
  size_t lineStart_ = 0;
  bool atLineStart_ = true;
};

}  // namespace tf
