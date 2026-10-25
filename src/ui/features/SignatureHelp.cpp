#include "ui/features/SignatureHelp.h"

#include <algorithm>

#include "ui/widgets/TextView.h"

namespace tf::ui {

namespace {
constexpr int kMaxLinesBack = 20;
}

std::optional<Position> SignatureHelp::openParen(const TextBuffer& b, Position caret) {
  caret = b.clamp(caret);
  int depth = 0;
  for (int l = caret.line; l >= 0 && l > caret.line - kMaxLinesBack; --l) {
    const std::string line = b.line(l);
    int c = l == caret.line ? std::min(caret.column, static_cast<int>(line.size())) : static_cast<int>(line.size());
    for (--c; c >= 0; --c) {
      const char ch = line[static_cast<size_t>(c)];
      if (ch == ')' || ch == ']' || ch == '}') ++depth;
      else if (ch == '(' || ch == '[' || ch == '{') {
        if (depth == 0) {
          if (ch == '(') return Position{l, c};
          return std::nullopt;  // an unmatched '[' or '{': not a call
        }
        --depth;
      } else if (ch == ';' && depth == 0) {
        return std::nullopt;  // left the statement
      }
    }
  }
  return std::nullopt;
}

int SignatureHelp::commasBefore(const TextBuffer& b, Position paren, Position caret) {
  int depth = 0, commas = 0;
  char quote = 0;
  for (int l = paren.line; l <= caret.line; ++l) {
    const std::string line = b.line(l);
    int from = l == paren.line ? paren.column + 1 : 0;
    const int to =
        l == caret.line ? std::min(caret.column, static_cast<int>(line.size())) : static_cast<int>(line.size());
    for (int c = from; c < to; ++c) {
      const char ch = line[static_cast<size_t>(c)];
      if (quote) {
        if (ch == '\\') ++c;
        else if (ch == quote) quote = 0;
      } else if (ch == '"' || ch == '\'') quote = ch;
      else if (ch == '(' || ch == '[' || ch == '{') ++depth;
      else if (ch == ')' || ch == ']' || ch == '}') --depth;
      else if (ch == ',' && depth == 0) ++commas;
    }
    quote = 0;  // a literal does not continue over a line break
  }
  return commas;
}

SignatureHelp::SignatureHelp(PickerEnv& env) : env_(env) {
  box_ = static_cast<PopupBox*>(env_.root.addChild(std::make_unique<PopupBox>()));
  box_->setMaxRows(8);
}

SignatureHelp::~SignatureHelp() {
  alive_.reset();
  if (box_ && box_->root()) box_->root()->removeChild(box_);
}

void SignatureHelp::close() {
  ++seq_;
  if (!open_) return;
  open_ = false;
  box_->hide();
}

void SignatureHelp::onTyped(const std::string& text) {
  if (text != "(" && text != ",") {
    update();
    return;
  }
  Document* d = env_.editor.currentDocument();
  if (!d || env_.editor.selections().empty()) return;
  const auto p = openParen(d->buffer(), env_.editor.selections()[0].head);
  if (!p) return;
  if (open_ && *p == paren_ && doc_ == d) {
    update();
    return;
  }
  doc_ = d;
  request(*p);
}

void SignatureHelp::request(Position paren) {
  paren_ = paren;
  const uint64_t seq = ++seq_;
  auto cb = [this, seq, alive = std::weak_ptr<int>(alive_)](const LspSignatureHelp& h, const std::string& err) {
    if (!alive.expired()) onResult(seq, h, err);
  };
  if (sig_) sig_(cb);
  else if (env_.editor.started()) env_.editor.signatureHelp(cb);
}

void SignatureHelp::onResult(uint64_t seq, const LspSignatureHelp& h, const std::string& err) {
  if (seq != seq_) return;
  Document* d = env_.editor.currentDocument();
  if (!err.empty() || h.signatures.empty() || d != doc_ || env_.editor.selections().empty()) {
    close();
    return;
  }
  const auto p = openParen(d->buffer(), env_.editor.selections()[0].head);
  if (!p || *p != paren_) {  // the caret left that call while clangd was thinking
    close();
    return;
  }
  help_ = h;
  help_.activeSignature = std::clamp(h.activeSignature, 0, static_cast<int>(h.signatures.size()) - 1);
  active_ = commasBefore(d->buffer(), paren_, env_.editor.selections()[0].head);
  open_ = true;
  render();
  placed_ = {};
  update();
}

void SignatureHelp::render() {
  const auto& sig = help_.signatures[static_cast<size_t>(help_.activeSignature)];
  using S = PopupSpan::Style;
  PopupLine l;
  size_t a = 0, b = 0;  // byte range of the active parameter inside the label
  if (active_ >= 0 && active_ < static_cast<int>(sig.parameters.size())) {
    const size_t open = sig.label.find('(');
    size_t from = open == std::string::npos ? 0 : open + 1;
    for (int i = 0; i < active_; ++i) {  // parameters are substrings in order: skip the earlier ones
      const size_t at = sig.label.find(sig.parameters[static_cast<size_t>(i)], from);
      if (at != std::string::npos) from = at + sig.parameters[static_cast<size_t>(i)].size();
    }
    a = sig.label.find(sig.parameters[static_cast<size_t>(active_)], from);
    if (a != std::string::npos) b = a + sig.parameters[static_cast<size_t>(active_)].size();
    else a = b = 0;
  }
  if (b > a) {
    l.spans.push_back({sig.label.substr(0, a), S::Text, false});
    l.spans.push_back({sig.label.substr(a, b - a), S::Hit, true});
    l.spans.push_back({sig.label.substr(b), S::Text, false});
  } else l.spans.push_back({sig.label, S::Text, false});
  if (help_.signatures.size() > 1)
    l.right = std::to_string(help_.activeSignature + 1) + "/" + std::to_string(help_.signatures.size());
  std::vector<PopupLine> lines{l};
  if (!sig.documentation.empty())
    for (const std::string& d : PopupBox::wrap(sig.documentation, 90))
      lines.push_back({{{d, S::Dim, false}}, "", false});
  box_->setContent(std::move(lines));
}

void SignatureHelp::update() {
  if (!open_) return;
  Document* d = env_.editor.currentDocument();
  auto* v = dynamic_cast<TextView*>(env_.root.focused());
  if (!d || d != doc_ || !v || env_.editor.selections().empty()) {
    close();
    return;
  }
  const Position caret = env_.editor.selections()[0].head;
  const auto p = openParen(d->buffer(), caret);
  if (!p) {
    close();
    return;
  }
  if (*p != paren_) {  // the inner call ended (or a new one opened): ask for the call around the caret
    request(*p);
    return;
  }
  const int a = commasBefore(d->buffer(), paren_, caret);
  if (a != active_) {
    active_ = a;
    render();
  }
  const RectF r = v->caretRect(paren_);
  if (r.x != placed_.x || r.y != placed_.y || !box_->visible()) {
    box_->showAt(r);
    placed_ = r;
  }
}

bool SignatureHelp::handleKey(const InputEvent& e) {
  if (!open_ || e.type != InputType::KeyDown || e.key != Key::Escape || e.mods) return false;
  close();
  return true;
}

}  // namespace tf::ui
