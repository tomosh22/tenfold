#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "ui/features/PickerUtil.h"
#include "ui/features/PopupBox.h"

namespace tf::ui {

// Signature help (B5b): typing `(` or `,` inside a call asks Editor::signatureHelp (clangd only; silent without it) and
// shows the signature under the call's `(` with the active parameter highlighted. The active parameter is counted
// locally from the top-level commas between the `(` and the caret (strings and nested ()[]{} skipped), so it follows
// the caret without a round trip; the popup closes when the caret leaves the call (`)` typed, caret before the `(`)
// or on Escape, and switches to the enclosing call when the inner one ends.
class SignatureHelp {
 public:
  using Done = std::function<void(const LspSignatureHelp&, const std::string& error)>;
  using SigFn = std::function<void(Done)>;

  explicit SignatureHelp(PickerEnv& env);
  ~SignatureHelp();
  SignatureHelp(const SignatureHelp&) = delete;
  SignatureHelp& operator=(const SignatureHelp&) = delete;

  void setSigFn(SigFn fn) { sig_ = std::move(fn); }  // default: Editor::signatureHelp
  void onTyped(const std::string& text);             // after text input was applied
  void update();                                     // after any other event
  bool handleKey(const InputEvent& e);               // Escape closes
  void close();
  bool isOpen() const { return open_; }
  PopupBox* box() const { return box_; }
  int activeParameter() const { return active_; }

  // The unmatched `(` before `caret` (same statement, at most 20 lines back) and the number of top-level commas
  // between it and the caret. Exposed for tests.
  static std::optional<Position> openParen(const TextBuffer& b, Position caret);
  static int commasBefore(const TextBuffer& b, Position paren, Position caret);

 private:
  void request(Position paren);
  void onResult(uint64_t seq, const LspSignatureHelp& h, const std::string& err);
  void render();

  PickerEnv& env_;
  SigFn sig_;
  PopupBox* box_ = nullptr;
  LspSignatureHelp help_;
  Position paren_{0, 0};
  const Document* doc_ = nullptr;
  RectF placed_{0, 0, 0, 0};
  uint64_t seq_ = 0;
  int active_ = 0;
  bool open_ = false;
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
