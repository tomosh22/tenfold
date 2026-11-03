#pragma once
#include <functional>
#include <string>
#include <vector>

#include "ui/platform/Input.h"

namespace tf::ui {

// Key-sequence recording (RecordKeySequence, Control Shift R) and playback (PlaybackKeySequence, Control Shift P),
// W8f. A recording is a list of steps at the command level, not raw SDL events:
//  * Command: a command that ran through EditorApp::runCommandId (a key binding, the command palette, a chord), by its
//    tenfold id and argument. The record / playback commands themselves and the palette are never recorded.
//  * Text: text typed into the editor (an InputType::Text event that reached the focused editor view).
//  * Key: a key press in the editor that has no binding (and so went to the view as a plain key).
// Only the "editor" key context is recorded: while a picker, the find bar or another input has the keys, nothing is
// (mouse clicks are not recorded at all). Adjacent Text steps are merged. Starting a new recording replaces the last
// one; stopping a recording with nothing recorded keeps the previous recording.
class KeySequence {
 public:
  struct Step {
    enum class Kind { Command, Text, Key } kind = Kind::Command;
    std::string id;    // Command: tenfold id
    std::string arg;   // Command: argument
    std::string text;  // Text
    InputEvent key;    // Key
  };

  bool recording() const { return recording_; }
  bool playing() const { return playing_; }
  const std::vector<Step>& steps() const { return last_; }  // the last finished recording
  size_t recordedSoFar() const { return current_.size(); }

  void start();
  // Ends the recording; false (and the old recording is kept) when nothing was recorded.
  bool stop();

  void recordCommand(const std::string& id, const std::string& arg);
  void recordText(const std::string& text);
  void recordKey(const InputEvent& e);

  struct PlayHooks {
    std::function<void(const std::string& id, const std::string& arg)> command;
    std::function<void(const InputEvent&)> input;  // Text and Key steps, sent to the focused widget
  };
  // Replays the last recording once; returns the number of steps played. Nothing is recorded meanwhile.
  size_t play(const PlayHooks& h);

 private:
  bool recording_ = false, playing_ = false;
  std::vector<Step> current_, last_;
};

}  // namespace tf::ui
