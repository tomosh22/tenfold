#include "ui/features/KeySequence.h"

namespace tf::ui {

void KeySequence::start() {
  recording_ = true;
  current_.clear();
}

bool KeySequence::stop() {
  recording_ = false;
  if (current_.empty()) return false;
  last_ = std::move(current_);
  current_.clear();
  return true;
}

void KeySequence::recordCommand(const std::string& id, const std::string& arg) {
  if (!recording_ || playing_) return;
  Step s;
  s.kind = Step::Kind::Command;
  s.id = id;
  s.arg = arg;
  current_.push_back(std::move(s));
}

void KeySequence::recordText(const std::string& text) {
  if (!recording_ || playing_ || text.empty()) return;
  if (!current_.empty() && current_.back().kind == Step::Kind::Text) {
    current_.back().text += text;
    return;
  }
  Step s;
  s.kind = Step::Kind::Text;
  s.text = text;
  current_.push_back(std::move(s));
}

void KeySequence::recordKey(const InputEvent& e) {
  if (!recording_ || playing_) return;
  Step s;
  s.kind = Step::Kind::Key;
  s.key = e;
  current_.push_back(std::move(s));
}

size_t KeySequence::play(const PlayHooks& h) {
  if (playing_ || recording_) return 0;
  playing_ = true;
  const std::vector<Step> steps = last_;  // a hook cannot change what is replayed
  for (const Step& s : steps) {
    switch (s.kind) {
      case Step::Kind::Command:
        if (h.command) h.command(s.id, s.arg);
        break;
      case Step::Kind::Text: {
        InputEvent e;
        e.type = InputType::Text;
        e.text = s.text;
        if (h.input) h.input(e);
        break;
      }
      case Step::Kind::Key:
        if (h.input) h.input(s.key);
        break;
    }
  }
  playing_ = false;
  return steps.size();
}

}  // namespace tf::ui
