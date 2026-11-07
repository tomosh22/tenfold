#include "ui/features/ExplorerPrompt.h"

namespace tf::ui {

namespace {
std::string trimmed(const std::string& s) {
  const size_t b = s.find_first_not_of(" \t");
  if (b == std::string::npos) return {};
  return s.substr(b, s.find_last_not_of(" \t") - b + 1);
}
}  // namespace

ExplorerPrompt::ExplorerPrompt(PickerEnv& env) : env_(env) {}

ExplorerPrompt::~ExplorerPrompt() {
  life_.reset();
  if (popup_) popup_->close();
}

void ExplorerPrompt::close() {
  if (popup_) popup_->close();
}

void ExplorerPrompt::askName(const std::string& title, const std::string& verb, const std::string& initial,
                             Validate validate, Named ok) {
  close();
  title_ = title;
  verb_ = verb;
  validate_ = std::move(validate);
  named_ = std::move(ok);
  auto p = std::make_unique<ListPopup>(true);
  p->input()->setPlaceholder(title);
  p->setMaxVisibleRows(1);
  p->setEmptyText("Type a name");
  p->onQuery([this](const std::string& q) { updateName(q); });
  p->onAccept([this, life = std::weak_ptr<int>(life_)](int) {
    if (life.expired()) return;
    const std::string typed = text_, name = trimmed(typed);
    const std::string why = validate_ ? validate_(name) : std::string();
    if (!why.empty()) {
      env_.say(why);
      const std::string t = title_, v = verb_;
      Validate val = validate_;
      Named n = named_;
      askName(t, v, typed, std::move(val), std::move(n));  // try again with what was typed
      return;
    }
    Named n = named_;
    if (n) n(name);
  });
  p->onClosed([this, life = std::weak_ptr<int>(life_)] {
    if (!life.expired()) popup_ = nullptr;
  });
  popup_ = ListPopup::open(env_.root, std::move(p));
  popup_->input()->setText(initial, true);
  updateName(initial);
}

void ExplorerPrompt::updateName(const std::string& text) {
  text_ = text;
  if (!popup_) return;
  const std::string name = trimmed(text);
  if (name.empty()) {
    popup_->setRows({}, -1);
    return;
  }
  const std::string why = validate_ ? validate_(name) : std::string();
  ListRow r;
  r.key = "name";
  r.primary = verb_ + " " + name;
  r.secondary = why;
  r.dim = !why.empty();
  popup_->setRows({r}, 0);
}

void ExplorerPrompt::confirm(const std::string& title, const std::vector<std::string>& details,
                             const std::string& okLabel, std::function<void()> ok) {
  close();
  auto p = std::make_unique<ListPopup>(false);
  p->setTitle(title);
  std::vector<ListRow> rows;
  for (const std::string& d : details) {
    ListRow r;
    r.key = "detail";
    r.primary = d;
    r.dim = true;
    rows.push_back(std::move(r));
  }
  const int first = static_cast<int>(rows.size());
  ListRow yes;
  yes.key = "ok";
  yes.primary = okLabel;
  rows.push_back(std::move(yes));
  ListRow no;
  no.key = "cancel";
  no.primary = "Cancel";
  rows.push_back(std::move(no));
  p->setMaxVisibleRows(static_cast<int>(rows.size()));
  p->setRows(std::move(rows), first);
  p->onAccept([first, ok = std::move(ok), life = std::weak_ptr<int>(life_)](int row) {
    if (life.expired() || row != first) return;  // a detail line or Cancel
    if (ok) ok();
  });
  p->onClosed([this, life = std::weak_ptr<int>(life_)] {
    if (!life.expired()) popup_ = nullptr;
  });
  popup_ = ListPopup::open(env_.root, std::move(p));
}

void ExplorerPrompt::info(const std::string& title, const std::vector<std::pair<std::string, std::string>>& rows) {
  close();
  auto p = std::make_unique<ListPopup>(false);
  p->setTitle(title);
  std::vector<ListRow> out;
  for (const auto& kv : rows) {
    ListRow r;
    r.key = kv.first;
    r.primary = kv.first + ":";
    r.secondary = kv.second;
    out.push_back(std::move(r));
  }
  p->setMaxVisibleRows(static_cast<int>(out.size()));
  p->setRows(std::move(out), 0);
  p->onClosed([this, life = std::weak_ptr<int>(life_)] {
    if (!life.expired()) popup_ = nullptr;
  });
  popup_ = ListPopup::open(env_.root, std::move(p));
}

}  // namespace tf::ui
