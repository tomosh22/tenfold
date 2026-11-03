#include "ui/commands/KeyMap.h"

#include <algorithm>

namespace tf::ui {

KeyChord chordFromKey(Key k, uint8_t mods) {
  KeyChord c;
  c.key = k;
  c.mods = mods;
  return c;
}

KeyChord chordFromMouse(MouseButton b, uint8_t mods) {
  KeyChord c;
  c.mouse = true;
  c.button = b;
  c.mods = mods;
  return c;
}

std::string formatChord(const KeyChord& c) {
  std::string s;
  if (c.mods & ModCtrl) s += "Control ";
  if (c.mods & ModShift) s += "Shift ";
  if (c.mods & ModAlt) s += "Alt ";
  if (c.mods & ModSuper) s += "Super ";
  if (c.mouse) {
    static const char* const names[] = {"MouseLeftButton", "MouseMiddleButton", "MouseRightButton", "MouseBackButton",
                                        "MouseForwardButton"};
    return s + names[static_cast<int>(c.button)];
  }
  const uint32_t k = static_cast<uint32_t>(c.key);
  if (k >= static_cast<uint32_t>(Key::F1) && k <= static_cast<uint32_t>(Key::F24))
    return s + "F" + std::to_string(k - static_cast<uint32_t>(Key::F1) + 1);
  if (k == ',') return s + "Comma";
  if (k > ' ' && k < 127) return s + static_cast<char>(k);
  switch (c.key) {
    case Key::Space: return s + "Space";
    case Key::Backspace: return s + "Backspace";
    case Key::Tab: return s + "Tab";
    case Key::Enter: return s + "Enter";
    case Key::KpEnter: return s + "KpEnter";
    case Key::Escape: return s + "Escape";
    case Key::Insert: return s + "Insert";
    case Key::Delete: return s + "Delete";
    case Key::Home: return s + "Home";
    case Key::End: return s + "End";
    case Key::PageUp: return s + "PageUp";
    case Key::PageDown: return s + "PageDown";
    case Key::Left: return s + "Left";
    case Key::Right: return s + "Right";
    case Key::Up: return s + "Up";
    case Key::Down: return s + "Down";
    case Key::CapsLock: return s + "CapsLock";
    case Key::Menu: return s + "AppsKey";
    case Key::PrintScreen: return s + "PrintScreen";
    case Key::Pause: return s + "Cancel";  // 10x's name for the Pause/Break key (Control+Break)
    default: return s + "?";
  }
}

std::string formatChords(const std::vector<KeyChord>& seq) {
  std::string s;
  for (auto& c : seq) {
    if (!s.empty()) s += ", ";
    s += formatChord(c);
  }
  return s;
}

bool KeyMap::add(KeyBinding b) {
  for (auto& e : bindings_)
    if (e.chords == b.chords && e.context == b.context) {
      e = std::move(b);
      return true;
    }
  bindings_.push_back(std::move(b));
  return false;
}

bool KeyMap::remove(const std::vector<KeyChord>& chords, std::string_view context) {
  for (size_t i = 0; i < bindings_.size(); ++i)
    if (bindings_[i].chords == chords && bindings_[i].context == context) {
      bindings_.erase(bindings_.begin() + static_cast<std::ptrdiff_t>(i));
      return true;
    }
  return false;
}

bool KeyMap::tick(int64_t nowMs) {
  if (pending_.empty() || nowMs - lastMs_ <= timeoutMs) return false;
  pending_.clear();
  return true;
}

KeyMap::Result KeyMap::feed(const KeyChord& c, std::string_view activeContext, int64_t nowMs) {
  tick(nowMs);
  struct Scan {
    const KeyBinding* exact = nullptr;  // a context-scoped binding beats a global one
    bool longer = false;
  };
  auto scan = [&](const std::vector<KeyChord>& seq) {
    Scan s;
    for (auto& b : bindings_) {
      if (b.context != "global" && b.context != activeContext) continue;
      if (b.chords.size() < seq.size() || !std::equal(seq.begin(), seq.end(), b.chords.begin())) continue;
      if (b.chords.size() == seq.size()) {
        if (!s.exact || (s.exact->context == "global" && b.context != "global")) s.exact = &b;
      } else {
        s.longer = true;
      }
    }
    return s;
  };

  bool wasPending = !pending_.empty();
  std::vector<KeyChord> seq = pending_;
  seq.push_back(c);
  const bool optional = pendingOptional_;
  pending_.clear();
  pendingOptional_ = false;
  Scan s = scan(seq);
  if (!s.exact && !s.longer && wasPending && optional) {  // the open chord already ran: this is a fresh stroke
    wasPending = false;
    seq = {c};
    s = scan(seq);
  }

  Result r;
  if (s.exact) {
    r.status = KeyMap::Status::Matched;
    r.binding = s.exact;
    if (s.longer) {  // keep the longer chord reachable: "Control S" saves, a second one saves everything
      pending_ = std::move(seq);
      pendingOptional_ = true;
      lastMs_ = nowMs;
    }
  } else if (s.longer) {
    pending_ = std::move(seq);
    lastMs_ = nowMs;
    r.status = KeyMap::Status::Pending;
    r.pendingText = formatChords(pending_) + "\xE2\x80\xA6";  // "…"
  } else {
    r.abortedChord = wasPending;
  }
  return r;
}

}  // namespace tf::ui
