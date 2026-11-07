#pragma once
#include <string_view>
#include <utility>
#include <vector>

#include "core/LineBreaks.h"

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
// handled. It never fails: unknown bytes become single-char Punct tokens. Comments, directives and strings end at "\r\n",
// "\n" or a lone '\r' (as in clang), but the line and column of a token count NEL, LS and PS as line breaks too, like
// the editor does (core/LineBreaks.h): a symbol on the line after an LS has the position the editor shows. A leading UTF-8 BOM is skipped (columns on line 1 still count its three bytes, like clangd's). With
// `isC` the C++-only keywords (class, delete, new, template, this, namespace, ...) are identifiers.
class CppLexer {
 public:
  explicit CppLexer(std::string_view src, bool isC = false) : s_(src), isC_(isC) {
    if (s_.size() >= 3 && s_.substr(0, 3) == "\xEF\xBB\xBF") i_ = 3;
    if (linebreak::hasSeparator(s_)) {
      for (size_t k = 0; k < s_.size(); ++k)
        if (const size_t n = linebreak::separatorLen(s_, k)) {
          seps_.emplace_back(k, n);
          k += n - 1;
        }
    }
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
  // Line and column of offset `off` (clang's line `clangLine` starting at `clangLineStart`) in the editor's lines.
  void place(size_t off, int clangLine, size_t clangLineStart, int& line, int& col);

  std::string_view s_;
  bool isC_ = false;
  size_t i_ = 0;
  int line_ = 0;
  size_t lineStart_ = 0;
  bool atLineStart_ = true;
  std::vector<std::pair<size_t, size_t>> seps_;  // offset and length of every NEL / LS / PS
  size_t sepIdx_ = 0;                            // seps_ before the last place()d offset
};

}  // namespace tf
