#include "ui/features/ConfigurationPicker.h"

#include <algorithm>
#include <cctype>

namespace tf::ui {

namespace {
std::string lowered(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}
}  // namespace

ConfigurationPicker::ConfigurationPicker(PickerEnv& env) : env_(env) { refresh(); }

void ConfigurationPicker::refresh() {
  configs_ = env_.editor.vsConfigurations();
  active_ = configs_.empty() ? std::string() : env_.editor.workspace().activeVsConfiguration().name();
}

std::string ConfigurationPicker::statusText() const {
  if (configs_.empty()) return {};
  if (!pending_.empty()) return "Configuration: " + pending_ + " (reloading...)";
  return active_;
}

void ConfigurationPicker::open() {
  close();
  refresh();
  if (configs_.empty()) {
    env_.say("No Visual Studio solution configurations in this workspace");
    return;
  }
  auto p = std::make_unique<ListPopup>(true);
  p->input()->setPlaceholder("Select solution configuration");
  p->setMaxVisibleRows(12);
  p->setEmptyText("No matching configuration");
  p->onQuery([this](const std::string& q) { fill(q); });
  p->onAccept([this](int row) {
    if (row < 0 || row >= static_cast<int>(shown_.size())) return;  // (the popup may already be closing)
    choose(shown_[static_cast<size_t>(row)]);
  });
  p->onClosed([this] { popup_ = nullptr; });
  popup_ = ListPopup::open(env_.root, std::move(p));
  fill({});
}

void ConfigurationPicker::close() {
  if (popup_) popup_->close();
}

void ConfigurationPicker::fill(const std::string& filter) {
  if (!popup_) return;
  const std::string f = lowered(filter);
  std::vector<ListRow> rows;
  shown_.clear();
  int sel = 0;
  bool activeShown = false;
  for (const auto& c : configs_) {
    const std::string name = c.name();
    if (!f.empty() && lowered(name).find(f) == std::string::npos) continue;
    ListRow r;
    r.key = name;
    r.primary = name;
    if (name == active_) {
      r.badge = "active";
      sel = static_cast<int>(rows.size());
      activeShown = true;
    }
    shown_.push_back(name);
    rows.push_back(std::move(r));
  }
  popup_->setRows(std::move(rows), activeShown ? sel : 0);  // else the first match
}

void ConfigurationPicker::choose(const std::string& name) {
  if (name == active_ && pending_.empty()) return;  // already active
  std::string err;
  pending_ = name;
  const bool ok = env_.editor.selectVsConfiguration(name, &err, [this, life = std::weak_ptr<int>(life_)] {
    if (life.expired()) return;
    pending_.clear();
    refresh();
  });
  if (!ok) {
    pending_.clear();
    env_.say("Cannot select configuration: " + err);
  }
}

}  // namespace tf::ui
