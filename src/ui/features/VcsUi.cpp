#include "ui/features/VcsUi.h"

#include <algorithm>

#include "ui/settings/Settings.h"
#include "ui/widgets/TextView.h"

namespace tf::ui {

namespace fs = std::filesystem;

VcsUi::VcsUi(PickerEnv& env, Hooks hooks) : env_(env), hooks_(std::move(hooks)) {
  env_.editor.vcs().onError([this](const fs::path&, const std::string& msg) { say("Version control: " + msg, 12000); });
  tool_.onError = [this](const std::string& m) { say(m, 12000); };
}

VcsUi::~VcsUi() {
  closeConfirm();
  env_.editor.vcs().onError(nullptr);
  tool_.onError = nullptr;
}

void VcsUi::say(const std::string& text, int ttlMs) const {
  if (hooks_.message) hooks_.message(text, ttlMs);
}

std::string VcsUi::relName(const fs::path& p) const {
  const VcsManager& v = env_.editor.vcs();
  return relativePath(v.active() ? v.root() : env_.editor.workspace().root(), p);
}

void VcsUi::applySettings(const Settings& s) {
  cfg_.tool = s.getString("vcs.diffTool");
  cfg_.args = s.getString("vcs.diffToolArgs");
}

void VcsUi::tick() { tool_.tick(); }

void VcsUi::refreshOnFocus() { env_.editor.vcs().refreshStatus(); }

void VcsUi::bind(CommandRegistry& r) {
  const CommandEnabled hasDoc = [](const CommandContext& c) { return c.editor && c.editor->currentDocument(); };
  auto add = [&](const char* id, std::function<void()> fn) {
    r.bind(
        id,
        [fn = std::move(fn)](const CommandContext&, std::string_view) {
          fn();
          return true;
        },
        hasDoc);
  };
  add("Vcs.NextChange", [this] { gotoChange(true); });
  add("Vcs.PrevChange", [this] { gotoChange(false); });
  add("Vcs.ToggleInlineDiff", [this] { toggleInlineDiff(); });
  add("Vcs.RevertHunk", [this] { revertHunk(); });
  add("Vcs.RevertFile", [this] { revertFile(); });
  add("Vcs.DiffFileExternal", [this] { diffExternal(); });
  add("Vcs.RefreshStatus", [this] { refreshStatus(); });
  add("Vcs.CheckOut", [this] { checkOut(); });
}

// ---- jumping ----

bool VcsUi::gotoChange(bool next) {
  Editor& ed = env_.editor;
  Document* d = ed.currentDocument();
  if (!d) return false;
  if (!ed.vcs().active() || !ed.vcs().hasBase(d->path())) {
    say("No version control information for " + relName(d->path()));
    return false;
  }
  TextView* v = hooks_.activeView ? hooks_.activeView() : nullptr;
  const std::vector<DiffHunk> hs =
      v && v->document() == d ? v->currentHunks() : diffLines(*ed.vcs().baseLines(d->path()), d->buffer().lines());
  if (hs.empty()) {
    say("No changes in " + relName(d->path()));
    return false;
  }
  const int last = d->buffer().lineCount() - 1;
  auto lineOf = [&](const DiffHunk& h) { return std::clamp(h.newStart, 0, last); };
  const int caret = d->buffer().clamp(ed.selections()[0].head).line;
  const DiffHunk* pick = nullptr;
  bool wrapped = false;
  if (next) {
    for (const DiffHunk& h : hs)
      if (lineOf(h) > caret) {
        pick = &h;
        break;
      }
    if (!pick) pick = &hs.front(), wrapped = true;
  } else {
    for (auto it = hs.rbegin(); it != hs.rend(); ++it)
      if (lineOf(*it) < caret) {
        pick = &*it;
        break;
      }
    if (!pick) pick = &hs.back(), wrapped = true;
  }
  std::string err;
  if (!ed.gotoLocation({d->path(), {lineOf(*pick), 0}}, &err, "change")) {
    say("Cannot go to the change: " + err);
    return false;
  }
  if (wrapped) say(next ? "Wrapped to the first change" : "Wrapped to the last change", 3000);
  return true;
}

// ---- hunk commands ----

bool VcsUi::toggleInlineDiff() {
  TextView* v = hooks_.activeView ? hooks_.activeView() : nullptr;
  if (!v || !v->document()) return false;
  if (v->toggleInlineDiff()) return true;
  say(v->diffStripActive() ? "No change at the caret" : "No version control information for this file");
  return false;
}

bool VcsUi::revertHunk() {
  Editor& ed = env_.editor;
  Document* d = ed.currentDocument();
  if (!d) return false;
  TextView* v = hooks_.activeView ? hooks_.activeView() : nullptr;
  if (!ed.vcs().active() || !ed.vcs().hasBase(d->path())) {
    say("No version control information for " + relName(d->path()));
    return false;
  }
  const std::vector<DiffHunk> hs =
      v && v->document() == d ? v->currentHunks() : diffLines(*ed.vcs().baseLines(d->path()), d->buffer().lines());
  const int k = TextView::hunkIndexAt(hs, d->buffer().clamp(ed.selections()[0].head).line);
  if (k < 0) {
    say("No change at the caret");
    return false;
  }
  std::string err;
  if (!ed.vcs().revertHunk(d->path(), static_cast<size_t>(k), &err)) {
    say("Cannot revert the change: " + err);
    return false;
  }
  return true;
}

// ---- whole file ----

void VcsUi::closeConfirm() {
  if (confirm_) confirm_->close();
}

void VcsUi::revertFile() {
  Editor& ed = env_.editor;
  Document* d = ed.currentDocument();
  if (!d) return;
  if (!ed.vcs().active()) {
    say("No version control system found for this workspace");
    return;
  }
  const VcsStatus st = ed.vcs().fileStatus(d->path());
  if (st == VcsStatus::Unmodified || st == VcsStatus::Unknown || st == VcsStatus::Ignored) {
    say("No changes to revert in " + relName(d->path()));
    return;
  }
  closeConfirm();
  const fs::path path = d->path();
  const std::string name = relName(path);
  auto p = std::make_unique<ListPopup>(false);
  p->setTitle("Revert " + name + "?");
  p->setMaxVisibleRows(2);
  ListRow yes, no;
  yes.key = "revert";
  yes.primary = "Discard all changes";
  yes.secondary = "unsaved edits too; this cannot be undone";
  no.key = "cancel";
  no.primary = "Cancel";
  p->setRows({yes, no}, 0);
  p->onAccept([this, path, name, life = std::weak_ptr<int>(life_)](int row) {
    if (life.expired() || row != 0) return;
    env_.editor.vcs().revertFile(path, [this, name, life](bool ok, const std::string& err) {
      if (life.expired()) return;
      say(ok ? "Reverted " + name : "Cannot revert " + name + ": " + err);
    });
  });
  p->onClosed([this, life = std::weak_ptr<int>(life_)] {
    if (!life.expired()) confirm_ = nullptr;
  });
  confirm_ = ListPopup::open(env_.root, std::move(p));
}

bool VcsUi::diffExternal() {
  Editor& ed = env_.editor;
  Document* d = ed.currentDocument();
  if (!d) return false;
  VcsManager& v = ed.vcs();
  if (!v.active()) {
    say("No version control system found for this workspace");
    return false;
  }
  const auto base = v.baseLines(d->path());
  if (!base) {
    say("No committed version of " + relName(d->path()) + " to compare with");
    return false;
  }
  DiffTool::Request r;
  r.file = d->path();
  r.title = relName(d->path());
  r.baseText = joinLines(*base, 0, base->size());
  if (d->buffer().lineEnding() == LineEnding::CRLF) {
    std::string crlf;
    for (char c : r.baseText) {
      if (c == '\n') crlf += '\r';
      crlf += c;
    }
    r.baseText = std::move(crlf);
  }
  r.currentText = d->buffer().textForDisk();
  r.baseLabel = v.providerName() == "perforce" ? "#have" : "@HEAD";
  r.currentLabel = d->dirty() ? " (unsaved)" : " (working copy)";
  const DiffTool::Launched l = tool_.launch(r, cfg_);
  if (!l.ok) {
    say(l.error, 12000);
    return false;
  }
  say("Opened " + fs::path(l.exe).filename().string() + " for " + r.title, 4000);
  return true;
}

void VcsUi::refreshStatus() {
  env_.editor.vcs().refreshStatus();
  say("Refreshing version control status...", 2000);
}

void VcsUi::checkOut() {
  Editor& ed = env_.editor;
  Document* d = ed.currentDocument();
  if (!d) return;
  if (!ed.vcs().active()) {
    say("No version control system found for this workspace");
    return;
  }
  const fs::path path = d->path();
  const std::string name = relName(path);
  ed.vcs().checkOut(path, [this, name, life = std::weak_ptr<int>(life_)](bool ok, const std::string&) {
    if (!life.expired() && ok) say("Checked out " + name, 4000);  // a failure was already reported through onError
  });
}

}  // namespace tf::ui
