#pragma once
#include <optional>
#include <string>

#include "core/Position.h"
#include "core/TextBuffer.h"
#include "semantic/SemanticEngine.h"

// Helpers of the 10x parser commands (W8c) that need no widgets, so they can be tested directly.
namespace tf::ui {

// FormatSelection without a selection formats "the current scope" (10x's wording): the innermost `{ }` block around
// `caret`, as whole lines from the line of the `{` to the line of its `}`. Comments, string and character literals
// and preprocessor lines are skipped while matching braces. Nullopt when the caret is not inside braces or the block
// is not closed.
std::optional<Range> enclosingBraceScope(const TextBuffer& b, Position caret);

// The range FormatSelection hands to clangd for a caret without selection: the enclosing brace scope, else the
// caret's line.
Range formatScopeRange(const TextBuffer& b, Position caret);

// A function, method, constructor or operator symbol that is defined here (has a body), as opposed to declared.
// Fast-tier symbols carry their own flag; clangd's document symbols are judged from `b`, the document text, by the
// symbol's extent: a top-level `{` before a top-level `;` means a body. Symbols without an extent count as defined.
bool isFunctionDefinition(const SymbolLocation& s, const TextBuffer& b);

}  // namespace tf::ui
