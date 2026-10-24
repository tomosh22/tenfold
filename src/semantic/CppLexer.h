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
// handled. It never fails: unknown bytes become single-char Punct tokens.
class CppLexer {
 public:
  explicit CppLexer(std::string_view src) : s_(src) {}
  Token next();
  std::vector<Token> all();
  static bool isKeyword(std::string_view s);

 private:
  void skipSpaceAndComments();
  Token make(Tok k, size_t start, int line, int col) const;

  std::string_view s_;
  size_t i_ = 0;
  int line_ = 0;
  size_t lineStart_ = 0;
  bool atLineStart_ = true;
};

}  // namespace tf
