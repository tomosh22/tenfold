#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ui/platform/Input.h"

namespace tf::ui {

// One stroke: a key or a mouse button, plus modifiers.
struct KeyChord {
  Key key = Key::Unknown;  // Unknown for mouse chords
  bool mouse = false;
  MouseButton button = MouseButton::Left;
  uint8_t mods = ModNone;
  bool operator==(const KeyChord&) const = default;
};

KeyChord chordFromKey(Key k, uint8_t mods);
KeyChord chordFromMouse(MouseButton b, uint8_t mods);
// 10x spelling: modifiers Control, Shift, Alt, then the key; the chords of a sequence are joined with ", ".
std::string formatChord(const KeyChord& c);                  // "Control Shift -", "MouseBackButton"
std::string formatChords(const std::vector<KeyChord>& seq);  // "Control K, Control C"

struct KeyBinding {
  std::vector<KeyChord> chords;    // 1 stroke, or several for a multi-stroke chord
  std::string context = "global";  // "global", "editor", "picker", ...
  std::string command;  // the tenfold command id; for a 10x command tenfold lacks, its 10x name (CommandNames.h)
  std::string arg;
  std::string written;  // the command exactly as the binding file spells it ("GotoSymbolDefinition")
};

// Binding table + chord state machine. Pure logic: the front end feeds key/mouse presses and a monotonic
// millisecond clock.
class KeyMap {
 public:
  enum class Status { NoMatch, Pending, Matched };
  struct Result {
    Status status = Status::NoMatch;
    const KeyBinding* binding = nullptr;  // Matched; valid until the map is modified
    std::string pendingText;              // Pending: "Ctrl+K…" for the status bar
    bool abortedChord = false;            // NoMatch: the key broke a pending chord and was swallowed
  };

  // Adds a binding; an existing one with identical chords and context is replaced (returns true then).
  bool add(KeyBinding b);
  // Removes the binding with these chords in this context; false if there was none.
  bool remove(const std::vector<KeyChord>& chords, std::string_view context);
  const std::vector<KeyBinding>& bindings() const { return bindings_; }

  // `activeContext` is the focused widget's context ("editor", "picker"); bindings scoped to it win over
  // "global" ones. A pending chord is dropped when more than timeoutMs passed since its last stroke, or when the
  // next stroke does not continue it (that stroke is swallowed and reported as abortedChord).
  // A stroke that is a complete binding and also the start of a longer one ("Control S" and "Control S, Control S")
  // matches at once and stays open for the longer chord; a following stroke that does not continue it is then an
  // ordinary fresh stroke, not swallowed.
  Result feed(const KeyChord& c, std::string_view activeContext, int64_t nowMs);
  bool pending() const { return !pending_.empty(); }
  void reset() { pending_.clear(); }
  // Expires a stale pending chord; returns true if the status bar text should be cleared.
  bool tick(int64_t nowMs);

  int timeoutMs = 1500;

 private:
  std::vector<KeyBinding> bindings_;
  std::vector<KeyChord> pending_;
  bool pendingOptional_ = false;  // pending_ is a complete binding that already ran
  int64_t lastMs_ = 0;
};

}  // namespace tf::ui
