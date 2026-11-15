#include "ui/shell/StartWindow.h"

#include <algorithm>

#include "core/PathUtil.h"
#include "ui/widgets/ContextMenu.h"
#include "ui/widgets/ListPopup.h"

namespace tf::ui {

namespace fs = std::filesystem;

namespace {
const char* const kActionTitle[] = {"Open a project or solution", "Open a local folder"};
const char* const kActionHint[] = {"A .sln, .slnx or .vcxproj file", "Open any folder as a workspace"};

std::string lowerAscii(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
}  // namespace

StartWindow::StartWindow(Hooks h) : hooks_(std::move(h)) {
  auto in = std::make_unique<InputLine>();
  search_ = static_cast<InputLine*>(addChild(std::move(in)));
  search_->setPlaceholder("Search recent");
  search_->onChanged([this](const std::string&) {
    refresh();
    selected_ = rows_.empty() ? -1 : 0;
    first_ = 0;
  });
  refresh();
}

bool StartWindow::sameAsShown() const {
  if (!hooks_.recent) return snapshot_.empty();
  return hooks_.recent->entries() == snapshot_ && search_->text() == shownFilter_;
}

void StartWindow::refresh(bool redraw) {
  rows_.clear();
  snapshot_.clear();
  const std::string q = search_->text();
  shownFilter_ = q;
  if (hooks_.recent) {
    snapshot_ = hooks_.recent->entries();
    for (const fs::path& e : snapshot_) {
      Row r;
      r.path = e;
      r.name = pathUtf8(e.filename().empty() ? e : e.filename());
      r.where = pathUtf8(e);
      if (!q.empty()) {
        r.match = fuzzyMatchPositions(q, r.name);
        if (r.match.empty() && lowerAscii(r.where).find(lowerAscii(q)) == std::string::npos) continue;
      }
      std::error_code ec;
      r.missing = !RecentSolutions::exists(e);
      r.folder = !r.missing && fs::is_directory(e, ec);
      rows_.push_back(std::move(r));
    }
  }
  selected_ = rows_.empty() ? -1 : std::clamp(selected_, 0, static_cast<int>(rows_.size()) - 1);
  if (selected_ < 0 && !rows_.empty()) selected_ = 0;
  first_ = std::clamp(first_, 0, std::max(0, static_cast<int>(rows_.size()) - 1));
  hoverRow_ = -1;
  if (redraw) markDirty();
}

std::string StartWindow::emptyText() const {
  if (!search_->text().empty()) return "No recent items match \"" + search_->text() + "\"";
  return "No recent projects, solutions or folders yet.";
}

StartWindow::Geo StartWindow::geo() const {
  const RectF& b = bounds();
  Geo g;
  const float lh = root() ? root()->text().lineHeight() : 14.0f;
  g.margin = 32;
  const float avail = std::max(0.0f, b.w - 3 * g.margin);
  g.leftW = std::max(280.0f, std::min(640.0f, avail * 0.6f));
  g.rightW = std::max(240.0f, std::min(360.0f, avail - g.leftW));
  g.leftX = b.x + g.margin;
  g.rightX = g.leftX + g.leftW + g.margin;
  g.titleY = b.y + g.margin;
  g.searchY = g.titleY + lh * 1.5f + theme().verticalPad;
  g.rowH = 2 * lh + 2 * theme().verticalPad + 2;
  g.listY = g.searchY + search_->preferredHeight() + theme().verticalPad * 2;
  g.listH = std::max(0.0f, b.y + b.h - g.margin - g.listY);
  return g;
}

int StartWindow::visibleRows() const {
  const Geo g = geo();
  return std::max(1, static_cast<int>(g.listH / g.rowH));
}

RectF StartWindow::rowRect(int i) const {
  const Geo g = geo();
  return {g.leftX, g.listY + (i - first_) * g.rowH, g.leftW, g.rowH};
}

RectF StartWindow::actionRect(int i) const {
  const Geo g = geo();
  return {g.rightX, g.listY + i * g.rowH, g.rightW, g.rowH};
}

int StartWindow::rowAt(float x, float y) const {
  const Geo g = geo();
  if (x < g.leftX || x >= g.leftX + g.leftW || y < g.listY || y >= g.listY + g.listH) return -1;
  const int i = first_ + static_cast<int>((y - g.listY) / g.rowH);
  return i >= 0 && i < static_cast<int>(rows_.size()) ? i : -1;
}

int StartWindow::actionAt(float x, float y) const {
  for (int i = 0; i < kActions; ++i)
    if (ui::contains(actionRect(i), x, y)) return i;
  return -1;
}

void StartWindow::onLayout() {
  const Geo g = geo();
  search_->layout({g.leftX, g.searchY, g.leftW, search_->preferredHeight()});
  ensureVisible();
}

void StartWindow::ensureVisible() {
  if (selected_ < 0) return;
  const int n = visibleRows();
  if (selected_ < first_) first_ = selected_;
  if (selected_ >= first_ + n) first_ = selected_ - n + 1;
  first_ = std::max(0, first_);
}

void StartWindow::setSelected(int i) {
  if (rows_.empty()) return;
  selected_ = std::clamp(i, 0, static_cast<int>(rows_.size()) - 1);
  ensureVisible();
  markDirty();
}

bool StartWindow::openSelected() {
  if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size()) || !hooks_.openRecent) return false;
  const fs::path p = rows_[static_cast<size_t>(selected_)].path;
  hooks_.openRecent(p);  // may replace the whole window: nothing may touch `this` afterwards
  return true;
}

bool StartWindow::removeRow(int i) {
  if (i < 0 || i >= static_cast<int>(rows_.size()) || !hooks_.recent) return false;
  const int keep = i;
  if (hooks_.recent->drop(rows_[static_cast<size_t>(i)].path)) hooks_.recent->save();
  selected_ = keep;
  refresh();
  ensureVisible();
  return true;
}

bool StartWindow::preKey(const InputEvent& e) {
  if (e.type != InputType::KeyDown || e.key != Key::Delete || (e.mods & (ModCtrl | ModAlt | ModShift | ModSuper)))
    return false;
  if (search_->hasSelection() || search_->caret() < search_->text().size()) return false;  // it edits the text
  return removeRow(selected_);
}

void StartWindow::runAction(int i) {
  if (i == 0 && hooks_.openProject) hooks_.openProject();
  else if (i == 1 && hooks_.openFolder) hooks_.openFolder();
}

void StartWindow::showMenu(int row, float x, float y) {
  if (row < 0 || !root()) return;
  setSelected(row);
  std::vector<MenuItem> items;
  MenuItem open;
  open.label = "Open";
  open.action = [this] { openSelected(); };
  items.push_back(std::move(open));
  MenuItem rm;
  rm.label = "Remove from list";
  rm.action = [this, row] { removeRow(row); };
  items.push_back(std::move(rm));
  ContextMenu::open(*root(), std::make_unique<ContextMenu>(std::move(items)), {x, y, 0, 0}, false);
}

void StartWindow::onHover(bool entered) {
  if (!entered && (hoverRow_ >= 0 || hoverAction_ >= 0)) {
    hoverRow_ = hoverAction_ = -1;
    markDirty();
  }
}

bool StartWindow::onEvent(const InputEvent& e) {
  switch (e.type) {
    case InputType::KeyDown: {
      if (e.mods & (ModCtrl | ModAlt | ModSuper)) return false;
      const int n = static_cast<int>(rows_.size());
      switch (e.key) {
        case Key::Down: setSelected(selected_ < 0 ? 0 : selected_ + 1); return true;
        case Key::Up: setSelected(selected_ < 0 ? 0 : selected_ - 1); return true;
        case Key::PageDown: setSelected(selected_ + visibleRows()); return true;
        case Key::PageUp: setSelected(selected_ - visibleRows()); return true;
        case Key::Enter:
        case Key::KpEnter: return openSelected();
        case Key::Escape:
          if (search_->text().empty()) return false;
          search_->setText("");
          refresh();
          selected_ = n ? 0 : -1;
          first_ = 0;
          return true;
        default: return false;
      }
    }
    case InputType::MouseMove: {
      const int r = rowAt(e.x, e.y), a = actionAt(e.x, e.y);
      if (r != hoverRow_ || a != hoverAction_) {
        hoverRow_ = r;
        hoverAction_ = a;
        markDirty();
      }
      return false;
    }
    case InputType::MouseDown: {
      const int r = rowAt(e.x, e.y), a = actionAt(e.x, e.y);
      if (e.button == MouseButton::Right && r >= 0) {
        showMenu(r, e.x, e.y);
        return true;
      }
      if (e.button != MouseButton::Left) return false;
      if (r >= 0) {
        setSelected(r);
        openSelected();
        return true;
      }
      if (a >= 0) {
        runAction(a);
        return true;
      }
      return false;
    }
    case InputType::Wheel: {
      const int n = static_cast<int>(rows_.size());
      const int maxFirst = std::max(0, n - visibleRows());
      first_ = std::clamp(first_ - static_cast<int>(e.dy * 3), 0, maxFirst);
      markDirty();
      return true;
    }
    default: return false;
  }
}

void StartWindow::paint(PaintContext& ctx) {
  if (!sameAsShown()) refresh(false);  // the list changed behind our back (File > Recent, another window's save)
  const Theme& t = ctx.theme;
  const RectF& b = bounds();
  ctx.fill(b, t.panelBackground);
  const Geo g = geo();
  const float lh = ctx.lineHeight(), pad = t.padding;
  ctx.text(g.leftX, g.titleY, "Open recent", t.text, b);
  ctx.text(g.rightX, g.titleY, "Get started", t.text, b);

  const RectF list{g.leftX, g.listY, g.leftW, g.listH};
  if (rows_.empty()) {
    const std::string msg = emptyText();
    ctx.text(g.leftX + pad, g.listY + t.verticalPad, msg, t.textDim, list);
    if (search_->text().empty())
      ctx.text(g.leftX + pad, g.listY + t.verticalPad + lh * 1.5f,
               "Projects, solutions and folders you open are listed here.", t.textDim, list);
  }
  const int n = static_cast<int>(rows_.size());
  for (int i = first_; i < n && i < first_ + visibleRows() + 1; ++i) {
    const Row& r = rows_[static_cast<size_t>(i)];
    const RectF rr = rowRect(i);
    if (rr.y + rr.h > g.listY + g.listH + 0.5f) break;
    if (i == selected_) ctx.fill(rr, t.selection);
    else if (i == hoverRow_) ctx.fill(rr, t.tabHover);
    const float x = rr.x + pad, y1 = rr.y + t.verticalPad + 1, y2 = y1 + lh;
    float right = rr.x + rr.w - pad;
    if (r.missing) {
      const float w = ctx.measure("missing");
      ctx.text(right - w, y1, "missing", t.dirtyMarker, rr);
      right -= w + 2 * ctx.textRenderer.cellWidth();
    }
    const Color base = r.missing ? t.textDim : t.text;
    const RectF clip1{x, rr.y, std::max(0.0f, right - x), rr.h};
    // The name, with the matched characters of the search drawn in the accent colour.
    // A name that does not fit ends in an ellipsis (highlights past the cut are dropped), the path loses its middle so
    // that the start and the file name stay readable (as Visual Studio shows long paths).
    const std::string shown = ctx.textRenderer.fit(r.name, clip1.w);
    const bool cut = shown != r.name;
    const size_t ellLen = ctx.textRenderer.ellipsis().size();
    const size_t limit = !cut ? r.name.size() : shown.size() >= ellLen ? shown.size() - ellLen : 0;
    float px = x;
    size_t from = 0, mi = 0;
    auto run = [&](size_t to, bool hit) {
      to = std::min(to, limit);
      if (to <= from) return;
      const std::string_view s = std::string_view(r.name).substr(from, to - from);
      ctx.text(px, y1, s, hit ? t.tabAccent : base, clip1);
      px += ctx.measure(s);
      from = to;
    };
    while (mi < r.match.size()) {
      const size_t at = static_cast<size_t>(r.match[mi]);
      run(at, false);
      run(at + 1, true);
      ++mi;
    }
    run(r.name.size(), false);
    if (cut) ctx.text(px, y1, std::string_view(shown).substr(limit), base, clip1);
    const RectF clip2{x, rr.y, std::max(0.0f, rr.x + rr.w - pad - x), rr.h};
    ctx.text(x, y2, ctx.textRenderer.fitMiddle(r.where, clip2.w), i == selected_ ? t.text : t.textDim, clip2);
  }
  if (n > visibleRows()) {  // scrollbar thumb
    Color c = t.textDim;
    c.a = 0.5f;
    const int vis = visibleRows();
    const float th = std::max(12.0f, g.listH * vis / n);
    ctx.fill({g.leftX + g.leftW - 4, g.listY + (g.listH - th) * first_ / std::max(1, n - vis), 3, th}, c);
  }

  for (int i = 0; i < kActions; ++i) {
    const RectF ar = actionRect(i);
    if (i == hoverAction_) ctx.fill(ar, t.tabHover);
    const float x = ar.x + pad, y1 = ar.y + t.verticalPad + 1;
    ctx.text(x, y1, kActionTitle[i], Theme::orElse(t.link, t.tabAccent), ar);
    ctx.text(x, y1 + lh, kActionHint[i], t.textDim, ar);
  }
}

}  // namespace tf::ui
