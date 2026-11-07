#include "ui/features/PerforceUi.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <functional>

#include "ui/settings/Settings.h"

namespace tf::ui {

namespace fs = std::filesystem;

namespace {
std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
}  // namespace

PerforceUi::PerforceUi(PickerEnv& env, Hooks hooks) : env_(env), hooks_(std::move(hooks)) {
  std::error_code ec;
  tempRoot_ = fs::temp_directory_path(ec) / "tenfold-p4";
}

PerforceUi::~PerforceUi() { close(); }

void PerforceUi::say(const std::string& text, int ttlMs) const {
  if (hooks_.message) hooks_.message(text, ttlMs);
}

std::string PerforceUi::relName(const fs::path& p) const {
  const VcsManager& v = env_.editor.vcs();
  return relativePath(v.active() ? v.root() : env_.editor.workspace().root(), p);
}

void PerforceUi::applySettings(const Settings& s) { p4vPath_ = s.getString("vcs.p4vPath"); }

void PerforceUi::close() {
  if (menu_) menu_->close();
  if (popup_) popup_->close();
}

std::string PerforceUi::formatTime(long long epochSeconds) {
  std::time_t t = static_cast<std::time_t>(epochSeconds);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
  return buf;
}

std::vector<std::string> PerforceUi::p4vArgs(const PerforceProvider::P4vTarget& t) {
  std::vector<std::string> a;
  if (!t.port.empty()) a.insert(a.end(), {"-p", t.port});
  if (!t.user.empty()) a.insert(a.end(), {"-u", t.user});
  if (!t.client.empty()) a.insert(a.end(), {"-c", t.client});
  if (!t.depotPath.empty()) a.insert(a.end(), {"-s", t.depotPath});
  return a;
}

// ---- PerforceShowInP4v ----

void PerforceUi::showInP4v() {
  Document* d = env_.editor.currentDocument();
  if (!d) return;
  if (d->isUntitled()) {
    say("Save the file first: it is not in Perforce");
    return;
  }
  const std::string exe = p4vPath_.empty() ? "p4v" : p4vPath_;
  env_.editor.vcs().perforceP4vTarget(d->path(),
                                      [this, exe, name = relName(d->path()), life = std::weak_ptr<int>(life_)](
                                          bool ok, PerforceProvider::P4vTarget t, const std::string& err) {
                                        if (life.expired()) return;
                                        if (!ok) {
                                          say("Cannot show " + name + " in p4v: " + err, 8000);
                                          return;
                                        }
                                        std::string e;
                                        const OsLauncher* l = hooks_.launcher ? &hooks_.launcher() : nullptr;
                                        if (!l || !l->startProgram || !l->startProgram(exe, p4vArgs(t), &e)) {
                                          say("Cannot start " + exe + (e.empty() ? std::string() : ": " + e), 8000);
                                          return;
                                        }
                                        say("Showing " + name + " in p4v", 4000);
                                      });
}

// ---- PerforceHistory ----

void PerforceUi::history() {
  Document* d = env_.editor.currentDocument();
  if (!d) return;
  if (d->isUntitled()) {
    say("Save the file first: it is not in Perforce");
    return;
  }
  const fs::path file = d->path();
  env_.editor.vcs().perforceHistory(file, [this, file, life = std::weak_ptr<int>(life_)](
                                              bool ok, VcsManager::Revisions revs, const std::string& err) {
    if (life.expired()) return;
    if (!ok) {
      say("Perforce history: " + err, 8000);
      return;
    }
    if (revs.empty()) {
      say(relName(file) + " is not in the Perforce depot");
      return;
    }
    // The workspace revision decides which actions the menu offers; a failure only hides "against have".
    env_.editor.vcs().perforceHaveRevision(
        file, [this, file, revs = std::move(revs), life](bool ok, int have, const std::string&) mutable {
          if (life.expired()) return;
          haveRev_ = ok ? have : 0;
          showRevisions(file, std::move(revs));
        });
  });
}

void PerforceUi::showRevisions(const fs::path& file, std::vector<PerforceProvider::Revision> revs) {
  close();
  file_ = file;
  revs_ = std::move(revs);
  auto p = std::make_unique<ListPopup>(true);
  p->input()->setPlaceholder("History of " + relName(file));
  p->setMaxVisibleRows(14);
  p->setEmptyText("No matching revisions");
  p->onQuery([this](const std::string&) { rebuild(); });
  p->onAccept([this](int row) {
    if (row >= 0 && row < static_cast<int>(shown_.size())) showActions(shown_[static_cast<size_t>(row)]);
  });
  p->onClosed([this] { popup_ = nullptr; });
  popup_ = ListPopup::open(env_.root, std::move(p));
  rebuild();
}

void PerforceUi::rebuild() {
  const std::string filter = lower(popup_->input()->text());
  shown_.clear();
  std::vector<ListRow> rows;
  for (const PerforceProvider::Revision& r : revs_) {
    ListRow row;
    row.key = std::to_string(r.rev);
    row.primary = "#" + std::to_string(r.rev) + "  change " + std::to_string(r.change) + "  " + formatTime(r.time) +
                  "  " + r.user;
    row.secondary = r.description;
    row.detail = r.action;
    if (!filter.empty() &&
        lower(row.primary + " " + row.secondary + " " + row.detail).find(filter) == std::string::npos)
      continue;
    rows.push_back(std::move(row));
    shown_.push_back(r);
  }
  popup_->setRows(std::move(rows), 0);
}

void PerforceUi::showActions(const PerforceProvider::Revision& rev) {
  if (menu_) menu_->close();
  enum class Act { Open, Previous, Have };
  struct Item {
    Act act;
    PerforceProvider::Revision other;
  };
  auto items = std::make_shared<std::vector<Item>>();
  std::vector<ListRow> rows;
  const std::string tag = "#" + std::to_string(rev.rev);
  auto add = [&](Act a, const std::string& key, const std::string& text, const std::string& detail,
                 PerforceProvider::Revision other = {}) {
    ListRow r;
    r.key = key;
    r.primary = text;
    r.secondary = detail;
    rows.push_back(std::move(r));
    items->push_back({a, std::move(other)});
  };
  add(Act::Open, "open", "Open " + tag, "read-only copy in a new tab");
  if (hooks_.diff) {
    auto older = std::find_if(revs_.begin(), revs_.end(), [&](const auto& r) { return r.rev < rev.rev; });
    if (older != revs_.end())
      add(Act::Previous, "previous", "Diff against previous revision (#" + std::to_string(older->rev) + ")",
          "external diff tool", *older);
    if (haveRev_ > 0 && haveRev_ != rev.rev) {
      auto have = std::find_if(revs_.begin(), revs_.end(), [&](const auto& r) { return r.rev == haveRev_; });
      PerforceProvider::Revision h;
      h.rev = haveRev_;
      if (have != revs_.end()) h = *have;
      add(Act::Have, "have", "Diff against have revision (#" + std::to_string(haveRev_) + ")", "external diff tool", h);
    }
  }
  auto p = std::make_unique<ListPopup>(false);
  p->setTitle("Revision " + tag + " of " + relName(file_));
  p->setMaxVisibleRows(3);
  p->setRows(std::move(rows), 0);
  p->onAccept([this, rev, items, life = std::weak_ptr<int>(life_)](int row) {
    if (life.expired() || row < 0 || row >= static_cast<int>(items->size())) return;
    const Item& it = (*items)[static_cast<size_t>(row)];
    if (it.act == Act::Open) openRevision(file_, rev);
    else diffRevisions(it.other, rev);  // older / have on the left, the chosen revision on the right
    if (popup_) popup_->close();        // the action is taken: the list goes too
  });
  p->onClosed([this, life = std::weak_ptr<int>(life_)] {
    if (!life.expired()) menu_ = nullptr;
  });
  menu_ = ListPopup::open(env_.root, std::move(p));
}

void PerforceUi::diffRevisions(const PerforceProvider::Revision& left, const PerforceProvider::Revision& right) {
  struct Pair {
    std::string a, b;
    int done = 0;
    bool failed = false;
  };
  auto pr = std::make_shared<Pair>();
  const fs::path file = file_;
  const std::string title = relName(file);
  auto finish = [this, pr, file, title, left, right] {
    if (pr->failed || pr->done < 2 || !hooks_.diff) return;
    DiffTool::Request r;
    r.file = file;
    r.title = title;
    r.baseText = pr->a;
    r.currentText = pr->b;
    r.baseLabel = "#" + std::to_string(left.rev);
    r.currentLabel = "#" + std::to_string(right.rev);
    hooks_.diff(r);
  };
  auto fetch = [&](int rev, bool isLeft) {
    env_.editor.vcs().perforceRevisionText(file, rev,
                                           [this, pr, finish, rev, title, isLeft, life = std::weak_ptr<int>(life_)](
                                               bool ok, std::string text, std::string err) {
                                             if (life.expired() || pr->failed) return;
                                             if (!ok) {
                                               pr->failed = true;
                                               say("Cannot read " + title + "#" + std::to_string(rev) + ": " + err,
                                                   8000);
                                               return;
                                             }
                                             (isLeft ? pr->a : pr->b) = std::move(text);
                                             ++pr->done;
                                             finish();
                                           });
  };
  fetch(left.rev, true);
  fetch(right.rev, false);
}

void PerforceUi::openRevision(const fs::path& file, const PerforceProvider::Revision& rev) {
  const std::string name = relName(file) + "#" + std::to_string(rev.rev);
  env_.editor.vcs().perforceRevisionText(
      file, rev.rev,
      [this, file, name, rev, life = std::weak_ptr<int>(life_)](bool ok, std::string text, const std::string& err) {
        if (life.expired()) return;
        if (!ok) {
          say("Cannot read " + name + ": " + err, 8000);
          return;
        }
        const fs::path dir = tempRoot_ / std::to_string(std::hash<std::string>{}(file.generic_string()) % 1000000007u);
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path out = dir / (file.stem().string() + "#" + std::to_string(rev.rev) + file.extension().string());
        fs::permissions(out, fs::perms::owner_write, fs::perm_options::add, ec);  // a previous read-only copy
        {
          std::ofstream o(out, std::ios::binary | std::ios::trunc);
          o << text;
          if (!o) {
            say("Cannot write " + out.string(), 8000);
            return;
          }
        }
        fs::permissions(out, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                        fs::perm_options::remove, ec);
        std::string e;
        if (!env_.editor.gotoLocation({out, {0, 0}}, &e, "p4 history")) {
          say("Cannot open " + name + ": " + e, 8000);
          return;
        }
        say("Opened " + name + " (read-only)", 4000);
      });
}

}  // namespace tf::ui
