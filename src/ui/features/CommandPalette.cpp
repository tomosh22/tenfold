#include "ui/features/CommandPalette.h"

#include <algorithm>
#include <cctype>
#include <tuple>

#include "ui/commands/CommandNames.h"

namespace tf::ui {

namespace {

std::string lowered(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// What a row shows: the 10x command name when 10x has the command, else tenfold's own name.
std::string labelOf(const Command& c) {
  const std::string_view n10 = displayName10x(c.id);
  return n10.empty() ? c.name : std::string(n10);
}

struct Hit {
  const Command* cmd = nullptr;
  std::string label;
  std::vector<int> match;  // positions in the label (empty when only the name or id matched)
  std::tuple<int, int, size_t> score{0, 0, 0};
};

// Lower is better: window beyond the query length, start of the match, candidate length.
std::tuple<int, int, size_t> scoreOf(const std::vector<int>& pos, size_t queryLen, size_t candidateLen) {
  return {static_cast<int>(pos.back() - pos.front() + 1) - static_cast<int>(queryLen), pos.front(), candidateLen};
}

}  // namespace

bool CommandPalette::listed(const Command& c) {
  return c.handler && c.id != "App.CommandPalette" && c.id != "Workbench.FocusPanel";
}

void CommandPalette::open() {
  close();
  auto p = std::make_unique<ListPopup>(true);
  p->input()->setPlaceholder("Type a command");
  p->setMaxVisibleRows(14);
  p->setEmptyText("No matching command");
  p->onQuery([this](const std::string&) { refresh(); });
  p->onAccept([this](int row) {
    if (row < 0 || row >= static_cast<int>(shown_.size())) return;
    const std::string id = shown_[static_cast<size_t>(row)];
    if (hooks_.run) hooks_.run(id);
  });
  p->onClosed([this] { popup_ = nullptr; });
  popup_ = ListPopup::open(env_.root, std::move(p));
  refresh();
}

void CommandPalette::close() {
  if (popup_) popup_->close();
}

void CommandPalette::refresh() {
  if (!popup_) return;
  std::string q;
  for (char c : popup_->input()->text())
    if (c != ' ') q += c;
  CommandContext ctx;
  ctx.editor = &env_.editor;
  ctx.context = "global";
  std::vector<Hit> hits;
  for (const Command* c : registry_.all()) {
    if (!listed(*c)) continue;
    Hit h;
    h.cmd = c;
    h.label = labelOf(*c);
    if (!q.empty()) {
      // The shown label (the 10x name when 10x has the command), tenfold's name and tenfold's id all match.
      std::vector<int> inLabel = fuzzyMatchPositions(q, h.label);
      std::vector<int> inName = h.label == c->name ? std::vector<int>{} : fuzzyMatchPositions(q, c->name);
      std::vector<int> inId = fuzzyMatchPositions(q, c->id);
      if (inLabel.empty() && inName.empty() && inId.empty()) continue;
      const std::tuple<int, int, size_t> none{1 << 20, 0, 0};
      const auto sl = inLabel.empty() ? none : scoreOf(inLabel, q.size(), h.label.size());
      const auto sn = inName.empty() ? none : scoreOf(inName, q.size(), c->name.size());
      const auto si = inId.empty() ? none : scoreOf(inId, q.size(), c->id.size());
      // The label wins ties and is the only one highlighted; a clearly tighter name or id match still ranks first.
      h.score = std::min({sl, sn, si});
      if (sl <= sn && sl <= si) h.match = std::move(inLabel);
    }
    hits.push_back(std::move(h));
  }
  std::stable_sort(hits.begin(), hits.end(), [&](const Hit& a, const Hit& b) {
    if (a.score != b.score) return a.score < b.score;
    return lowered(a.label) < lowered(b.label);
  });
  std::vector<ListRow> rows;
  shown_.clear();
  for (const Hit& h : hits) {
    ListRow r;
    r.key = h.cmd->id;
    r.primary = h.label;
    r.secondary = h.cmd->id;
    r.match = h.match;
    r.dim = !h.cmd->isEnabled(ctx);
    if (hooks_.binding) r.detail = hooks_.binding(h.cmd->id);
    shown_.push_back(h.cmd->id);
    rows.push_back(std::move(r));
  }
  popup_->setStatus(std::to_string(rows.size()) + " commands");
  popup_->setRows(std::move(rows), 0);
}

}  // namespace tf::ui
