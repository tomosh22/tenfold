#include "ui/widgets/TreeView.h"

#include <algorithm>
#include <cmath>

#include "core/PathUtil.h"
#include "semantic/FastIndex.h"

namespace tf::ui {

namespace {
std::string lowerAscii(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

std::vector<std::string_view> splitTokens(const std::string& q) {
  std::vector<std::string_view> out;
  for (size_t i = 0; i < q.size();) {
    while (i < q.size() && q[i] == ' ') ++i;
    size_t j = i;
    while (j < q.size() && q[j] != ' ') ++j;
    if (j > i) out.emplace_back(q.data() + i, j - i);
    i = j;
  }
  return out;
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// TreeModel
// ---------------------------------------------------------------------------------------------------------------

int TreeModel::add(int parent, std::string name, TreeKind kind, std::string path) {
  const int id = static_cast<int>(nodes_.size());
  TreeNode n;
  n.name = std::move(name);
  n.path = std::move(path);
  n.kind = kind;
  n.parent = parent;
  nodes_.push_back(std::move(n));
  if (parent >= 0) nodes_[static_cast<size_t>(parent)].kids.push_back(id);
  return id;
}

void TreeModel::sortKids(int id) {
  auto& kids = nodes_[static_cast<size_t>(id)].kids;
  if (kids.size() < 2) return;
  std::vector<std::pair<std::string, int>> keyed;
  keyed.reserve(kids.size());
  for (int k : kids) keyed.emplace_back(lowerAscii(nodes_[static_cast<size_t>(k)].name), k);
  std::stable_sort(keyed.begin(), keyed.end(), [&](const auto& a, const auto& b) {
    const bool af = nodes_[static_cast<size_t>(a.second)].kind == TreeKind::File;
    const bool bf = nodes_[static_cast<size_t>(b.second)].kind == TreeKind::File;
    if (af != bf) return !af;  // containers first
    return a.first < b.first;
  });
  for (size_t i = 0; i < kids.size(); ++i) kids[i] = keyed[i].second;
}

void TreeModel::sortAll() {
  for (size_t i = 0; i < nodes_.size(); ++i) sortKids(static_cast<int>(i));
}

void TreeModel::indexPaths() {
  byPath_.clear();
  byPath_.reserve(nodes_.size());
  for (size_t i = 0; i < nodes_.size(); ++i)
    if (!nodes_[i].path.empty())
      byPath_.emplace(pathKey(nodes_[i].path, nativePathStyle()), static_cast<int>(i));  // the first one wins
}

int TreeModel::findPath(const std::string& path) const {
  const auto it = byPath_.find(pathKey(path, nativePathStyle()));
  return it == byPath_.end() ? -1 : it->second;
}

int TreeModel::depthOf(int id) const {
  int d = 0;
  for (int p = nodes_[static_cast<size_t>(id)].parent; p >= 0; p = nodes_[static_cast<size_t>(p)].parent) ++d;
  return d;
}

std::string TreeModel::keyOf(int id) const {
  std::vector<int> chain;
  for (int n = id; n > 0; n = nodes_[static_cast<size_t>(n)].parent) chain.push_back(n);
  std::string key;
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    key += '\x1f';
    key += nodes_[static_cast<size_t>(*it)].name;
  }
  return key;
}

// ---------------------------------------------------------------------------------------------------------------
// TreeView: state
// ---------------------------------------------------------------------------------------------------------------

float TreeView::rowHeight() const {
  const RootWidget* r = root();
  const float lh = r ? r->text().lineHeight() : metricLh_;
  return std::ceil(lh + theme().verticalPad);
}

RectF TreeView::headerRect() const {
  const RectF& b = bounds();
  return {b.x, b.y, b.w, std::min(b.h, rowHeight())};
}

RectF TreeView::listRect() const {
  const RectF& b = bounds();
  const float h = std::min(b.h, rowHeight());
  return {b.x, b.y + h, b.w, std::max(0.0f, b.h - h)};
}

RectF TreeView::rowRect(int row) const {
  const RectF l = listRect();
  return {l.x, l.y + static_cast<float>(row) * rowHeight() - scrollY_, l.w, rowHeight()};
}

int TreeView::visibleRowCount() const {
  const float rh = rowHeight();
  return rh > 0 ? static_cast<int>(std::ceil(listRect().h / rh)) : 0;
}

int TreeView::firstVisibleRow() const {
  const float rh = rowHeight();
  return rh > 0 ? static_cast<int>(scrollY_ / rh) : 0;
}

int TreeView::rowAtY(float y) const {
  const RectF l = listRect();
  if (y < l.y || y >= l.y + l.h) return -1;
  const int r = static_cast<int>((y - l.y + scrollY_) / rowHeight());
  return r >= 0 && r < static_cast<int>(rows_.size()) ? r : -1;
}

RectF TreeView::arrowRect(int row) const {
  const RectF r = rowRect(row);
  const float cell = root() ? root()->text().cellWidth() : metricCell_;
  const int depth = row >= 0 && row < static_cast<int>(rows_.size()) ? rows_[static_cast<size_t>(row)].depth : 0;
  const float x = r.x + theme().padding + static_cast<float>(depth) * 2 * cell;
  return {x, r.y, 2 * cell, r.h};
}

RectF TreeView::thumbRect() const {
  const RectF l = listRect();
  const float ch = contentHeight();
  if (ch <= l.h || l.h <= 0) return {0, 0, 0, 0};
  const float th = std::max(16.0f, l.h * l.h / ch);
  const float maxScroll = ch - l.h;
  const float y = l.y + (l.h - th) * (maxScroll > 0 ? scrollY_ / maxScroll : 0);
  return {l.x + l.w - 6, y, 6, th};
}

void TreeView::clampScroll() {
  const float maxScroll = std::max(0.0f, contentHeight() - listRect().h);
  scrollY_ = std::clamp(scrollY_, 0.0f, maxScroll);
}

void TreeView::setScrollY(float y) {
  const float old = scrollY_;
  scrollY_ = y;
  clampScroll();
  if (scrollY_ != old) markDirty();
}

void TreeView::ensureVisible(int row, bool centre) {
  if (row < 0) return;
  const float rh = rowHeight(), top = static_cast<float>(row) * rh, h = listRect().h;
  if (centre) setScrollY(top - (h - rh) / 2);
  else if (top < scrollY_) setScrollY(top);
  else if (top + rh > scrollY_ + h) setScrollY(top + rh - h);
}

bool TreeView::shown(int node) const {
  if (node < 0 || node != selected_) return false;
  const int row = selectedRow();
  if (row < 0) return false;
  const float rh = rowHeight(), top = static_cast<float>(row) * rh;
  return top >= scrollY_ && top + rh <= scrollY_ + listRect().h + 0.5f;
}

int TreeView::selectedRow() const {
  if (selected_ < 0) return -1;
  for (size_t i = 0; i < rows_.size(); ++i)
    if (rows_[i].node == selected_) return static_cast<int>(i);
  return -1;
}

void TreeView::collectExpandedKeys(std::unordered_set<std::string>& out) const {
  if (model_.empty()) return;
  // Only expanded nodes are descended into, so the cost follows what the user opened, not the tree size.
  struct F {
    int node;
    std::string key;
  };
  std::vector<F> stack;
  stack.push_back({0, std::string()});
  while (!stack.empty()) {
    F f = std::move(stack.back());
    stack.pop_back();
    if (!isExpanded(f.node)) continue;
    out.insert(f.key);
    for (int k : model_.node(f.node).kids)
      if (isExpanded(k)) stack.push_back({k, f.key + '\x1f' + model_.node(k).name});
  }
}

void TreeView::applyExpandedKeys(const std::unordered_set<std::string>& keys) {
  std::fill(expanded_.begin(), expanded_.end(), 0);
  if (model_.empty()) return;
  struct F {
    int node;
    std::string key;
  };
  std::vector<F> stack;
  stack.push_back({0, std::string()});
  while (!stack.empty()) {
    F f = std::move(stack.back());
    stack.pop_back();
    if (!keys.count(f.key)) continue;
    expanded_[static_cast<size_t>(f.node)] = 1;
    for (int k : model_.node(f.node).kids)
      if (model_.node(k).expandable()) stack.push_back({k, f.key + '\x1f' + model_.node(k).name});
  }
}

void TreeView::setModel(TreeModel&& m) {
  const bool first = model_.empty();
  std::unordered_set<std::string> keys;
  std::string selKey;
  bool hadSel = false;
  if (!first) {
    collectExpandedKeys(keys);
    if (selected_ >= 0) {
      selKey = model_.keyOf(selected_);
      hadSel = true;
    }
  }
  model_ = std::move(m);
  expanded_.assign(model_.size(), 0);
  if (first || keys.empty()) {
    if (!model_.empty()) expanded_[0] = 1;
  } else {
    applyExpandedKeys(keys);
  }
  selected_ = -1;
  if (hadSel) {
    // Walk the key back down to the node.
    int cur = 0;
    size_t pos = 0;
    while (pos < selKey.size() && cur >= 0) {
      const size_t end = selKey.find('\x1f', pos + 1);
      const std::string name = selKey.substr(pos + 1, end == std::string::npos ? std::string::npos : end - pos - 1);
      int next = -1;
      for (int k : model_.node(cur).kids)
        if (model_.node(k).name == name) {
          next = k;
          break;
        }
      cur = next;
      pos = end == std::string::npos ? selKey.size() : end;
    }
    if (cur >= 0 && !selKey.empty()) selected_ = cur;
  }
  rebuildRows();
  clampScroll();
  markDirty();
}

void TreeView::rebuildRows() {
  rows_.clear();
  if (model_.empty()) return;
  const size_t n = model_.size();
  if (filter_.empty()) {
    std::vector<Row> stack;
    stack.push_back({0, 0});
    while (!stack.empty()) {
      const Row r = stack.back();
      stack.pop_back();
      rows_.push_back(r);
      if (!isExpanded(r.node)) continue;
      const auto& kids = model_.node(r.node).kids;
      for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back({*it, r.depth + 1});
    }
  } else {
    const std::vector<std::string_view> tokens = splitTokens(filter_);
    std::vector<uint8_t> has(n, 0);
    for (size_t i = 1; i < n; ++i) {
      const std::string& name = model_.node(static_cast<int>(i)).name;
      bool all = true;
      for (std::string_view t : tokens)
        if (FastIndex::fuzzyScore(t, name) <= 0) {
          all = false;
          break;
        }
      if (all) has[i] = 1;
    }
    for (size_t i = n; i-- > 1;)  // parents precede children, so one backwards sweep propagates to every ancestor
      if (has[i]) has[static_cast<size_t>(model_.node(static_cast<int>(i)).parent)] = 1;
    if (has[0]) {
      std::vector<Row> stack;
      stack.push_back({0, 0});
      while (!stack.empty()) {
        const Row r = stack.back();
        stack.pop_back();
        rows_.push_back(r);
        const auto& kids = model_.node(r.node).kids;
        for (auto it = kids.rbegin(); it != kids.rend(); ++it)
          if (has[static_cast<size_t>(*it)]) stack.push_back({*it, r.depth + 1});
      }
    }
  }
  if (selected_ >= 0 && selectedRow() < 0) selected_ = -1;
}

void TreeView::setExpanded(int node, bool on) {
  if (node < 0 || static_cast<size_t>(node) >= expanded_.size() || !model_.node(node).expandable()) return;
  if ((expanded_[static_cast<size_t>(node)] != 0) == on) return;
  expanded_[static_cast<size_t>(node)] = on ? 1 : 0;
  rebuildRows();
  clampScroll();
  markDirty();
}

void TreeView::expandAll(bool on) {
  for (size_t i = 0; i < expanded_.size(); ++i)
    expanded_[i] = (on || i == 0) && model_.node(static_cast<int>(i)).expandable() ? 1 : 0;
  rebuildRows();
  clampScroll();
  markDirty();
}

void TreeView::setFilter(const std::string& f) {
  if (f == filter_) return;
  filter_ = f;
  const int keep = selected_;
  rebuildRows();
  if (filter_.empty() && keep >= 0 && selected_ < 0) {  // the filter is gone: show where the selection lives
    for (int p = model_.node(keep).parent; p >= 0; p = model_.node(p).parent) expanded_[static_cast<size_t>(p)] = 1;
    selected_ = keep;
    rebuildRows();
  }
  if (selected_ < 0 && !filter_.empty() && !rows_.empty()) {
    // The old selection is hidden: land on the first matching file, else the first matching node.
    int firstAny = -1;
    for (const Row& r : rows_) {
      if (r.node == 0) continue;
      if (firstAny < 0) firstAny = r.node;
      if (model_.node(r.node).kind == TreeKind::File) {
        selected_ = r.node;
        break;
      }
    }
    if (selected_ < 0) selected_ = firstAny;
  }
  scrollY_ = 0;
  if (keep >= 0 && selected_ == keep) ensureVisible(selectedRow(), true);
  else ensureVisible(selectedRow());
  clampScroll();
  markDirty();
}

void TreeView::select(int node) {
  if (node < 0 || static_cast<size_t>(node) >= model_.size()) return;
  reveal(node);
}

bool TreeView::reveal(int node) {
  if (node < 0 || static_cast<size_t>(node) >= model_.size()) return false;
  bool changed = false;
  for (int p = model_.node(node).parent; p >= 0; p = model_.node(p).parent)
    if (!expanded_[static_cast<size_t>(p)]) {
      expanded_[static_cast<size_t>(p)] = 1;
      changed = true;
    }
  if (changed) rebuildRows();
  selected_ = node;
  int row = selectedRow();
  if (row < 0 && !filter_.empty()) {  // hidden by the filter
    filter_.clear();
    rebuildRows();
    selected_ = node;
    row = selectedRow();
  }
  if (row >= 0) {
    const float top = static_cast<float>(row) * rowHeight();
    if (top < scrollY_ || top + rowHeight() > scrollY_ + listRect().h) ensureVisible(row, true);
  }
  clampScroll();
  markDirty();
  return true;
}

void TreeView::setSelectedRow(int row, bool user) {
  if (row < 0 || row >= static_cast<int>(rows_.size())) return;
  const int node = rows_[static_cast<size_t>(row)].node;
  const bool changed = node != selected_;
  selected_ = node;
  ensureVisible(row);
  markDirty();
  if (changed && user && onSelected) onSelected(node);
}

void TreeView::moveSelection(int delta) {
  if (rows_.empty()) return;
  const int cur = selectedRow();
  const int next = cur < 0 ? (delta > 0 ? 0 : static_cast<int>(rows_.size()) - 1)
                           : std::clamp(cur + delta, 0, static_cast<int>(rows_.size()) - 1);
  setSelectedRow(next, true);
}

void TreeView::activateOrToggle(int node) {
  if (node < 0) return;
  if (model_.node(node).expandable()) {
    if (filter_.empty()) toggle(node);
  } else if (model_.node(node).kind == TreeKind::File && onActivate) {
    onActivate(node);
  }
}

// ---------------------------------------------------------------------------------------------------------------
// TreeView: input
// ---------------------------------------------------------------------------------------------------------------

void TreeView::onLayout() {
  clampScroll();
  if (RootWidget* r = root()) {
    metricLh_ = r->text().lineHeight();
    metricCell_ = r->text().cellWidth();
  }
}

void TreeView::onHover(bool entered) {
  if (!entered && hoverRow_ != -1) {
    hoverRow_ = -1;
    markDirty();
  }
}

void TreeView::onFocusChanged(bool gained) {
  focused_ = gained;
  markDirty();
}

bool TreeView::onEvent(const InputEvent& e) {
  switch (e.type) {
    case InputType::KeyDown: {
      if (e.mods & (ModCtrl | ModAlt | ModSuper)) {
        if ((e.mods & ModCtrl) && !(e.mods & (ModAlt | ModSuper))) {
          if (e.key == Key::Home) return setSelectedRow(0, true), true;
          if (e.key == Key::End) return setSelectedRow(static_cast<int>(rows_.size()) - 1, true), true;
        }
        return false;
      }
      const int page = std::max(1, visibleRowCount() - 1);
      const int row = selectedRow();
      switch (e.key) {
        case Key::Up: moveSelection(-1); return true;
        case Key::Down: moveSelection(1); return true;
        case Key::PageUp: moveSelection(-page); return true;
        case Key::PageDown: moveSelection(page); return true;
        case Key::Home: setSelectedRow(0, true); return true;
        case Key::End: setSelectedRow(static_cast<int>(rows_.size()) - 1, true); return true;
        case Key::Right:
          if (selected_ >= 0 && filter_.empty() && model_.node(selected_).expandable()) {
            if (!isExpanded(selected_)) setExpanded(selected_, true);
            else if (row >= 0 && row + 1 < static_cast<int>(rows_.size())) setSelectedRow(row + 1, true);
          }
          return true;
        case Key::Left:
          if (selected_ >= 0) {
            if (filter_.empty() && isExpanded(selected_) && selected_ != 0) setExpanded(selected_, false);
            else if (const int p = model_.node(selected_).parent; p >= 0) {
              for (size_t i = 0; i < rows_.size(); ++i)
                if (rows_[i].node == p) {
                  setSelectedRow(static_cast<int>(i), true);
                  break;
                }
            }
          }
          return true;
        case Key::Enter:
        case Key::KpEnter: activateOrToggle(selected_); return true;
        case Key::Backspace:
          if (!filter_.empty()) {
            size_t cut = filter_.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(filter_[cut]) & 0xC0) == 0x80) --cut;
            setFilter(filter_.substr(0, cut));
          }
          return true;
        case Key::Escape:
          if (!filter_.empty()) setFilter({});
          else if (onClose) onClose();
          return true;
        default: return false;
      }
    }
    case InputType::Text:
      if (e.text.empty() || static_cast<unsigned char>(e.text[0]) < 0x20) return false;
      setFilter(filter_ + e.text);
      return true;
    case InputType::MouseMove: {
      if (dragThumb_) {
        const RectF l = listRect(), t = thumbRect();
        const float track = l.h - t.h, maxScroll = contentHeight() - l.h;
        if (track > 0) setScrollY((e.y - l.y - dragGrab_) / track * maxScroll);
        return true;
      }
      const int h = rowAtY(e.y);
      if (h != hoverRow_ && contains(e.x, e.y)) {
        hoverRow_ = h;
        markDirty();
      }
      return true;
    }
    case InputType::MouseUp:
      if (dragThumb_) {
        dragThumb_ = false;
        releaseMouse();
        return true;
      }
      return false;
    case InputType::MouseDown: {
      if (e.button != MouseButton::Left) return true;
      const RectF t = thumbRect();
      if (t.h > 0 && ui::contains(t, e.x, e.y)) {
        dragThumb_ = true;
        dragGrab_ = e.y - t.y;
        captureMouse();
        return true;
      }
      const int row = rowAtY(e.y);
      if (row < 0) return true;
      const int node = rows_[static_cast<size_t>(row)].node;
      setSelectedRow(row, true);
      const bool onArrow = model_.node(node).expandable() && filter_.empty() && ui::contains(arrowRect(row), e.x, e.y);
      if (onArrow) toggle(node);
      else if (e.clicks >= 2) activateOrToggle(node);
      return true;
    }
    case InputType::Wheel: setScrollY(scrollY_ - e.dy * 3 * rowHeight()); return true;
    default: return false;
  }
}

// ---------------------------------------------------------------------------------------------------------------
// TreeView: painting
// ---------------------------------------------------------------------------------------------------------------

void TreeView::paint(PaintContext& ctx) {
  const Theme& th = ctx.theme;
  metricLh_ = ctx.lineHeight();
  metricCell_ = ctx.textRenderer.cellWidth();
  const RectF& b = bounds();
  ctx.fill(b, Theme::orElse(th.treeBackground, th.panelBackground));

  // Header: the filter text, or a hint.
  const RectF hd = headerRect();
  ctx.fill(hd, th.tabStripBackground);
  const float ty = hd.y + (hd.h - ctx.lineHeight()) / 2;
  if (filter_.empty()) ctx.text(hd.x + th.padding, ty, "Type to filter", th.textDim, hd);
  else {
    ctx.text(hd.x + th.padding, ty, filter_, th.text, hd);
    const std::string cnt = std::to_string(rows_.empty() ? 0 : rows_.size() - 1) + " shown";
    const float w = ctx.measure(cnt);
    ctx.text(hd.x + hd.w - th.padding - w, ty, cnt, th.textDim, hd);
  }

  const RectF list = listRect();
  const float rh = rowHeight(), cell = ctx.textRenderer.cellWidth(), pad = th.padding;
  const int first = firstVisibleRow();
  const int last = std::min(static_cast<int>(rows_.size()), first + visibleRowCount() + 1);
  for (int i = first; i < last; ++i) {
    const Row& r = rows_[static_cast<size_t>(i)];
    const TreeNode& nd = model_.node(r.node);
    const RectF rr = intersect(rowRect(i), list);
    if (rr.h <= 0) continue;
    if (r.node == selected_)
      ctx.fill(rr, focused_ ? Theme::orElse(th.treeSelection, th.selection)
                            : Theme::orElse(th.treeSelectionInactive, th.currentLine));
    else if (i == hoverRow_) ctx.fill(rr, th.tabHover);
    const RectF full = rowRect(i);
    const float x0 = full.x + pad + static_cast<float>(r.depth) * 2 * cell;
    const float cy = full.y + full.h / 2;
    const Color dim = th.textDim;
    if (nd.expandable()) {  // disclosure chevron
      const float ax = x0 + cell * 0.5f, s = std::max(2.0f, rh * 0.18f);
      if (isExpanded(r.node) || !filter_.empty()) {
        ctx.line(ax - s, cy - s / 2, ax, cy + s / 2, 1.5f, dim);
        ctx.line(ax, cy + s / 2, ax + s, cy - s / 2, 1.5f, dim);
      } else {
        ctx.line(ax - s / 2, cy - s, ax + s / 2, cy, 1.5f, dim);
        ctx.line(ax + s / 2, cy, ax - s / 2, cy + s, 1.5f, dim);
      }
    }
    // Icon: a folder tab, a project square, a file outline.
    const float ix = x0 + 2 * cell, is = std::max(6.0f, rh * 0.42f);
    switch (nd.kind) {
      case TreeKind::Folder:
      case TreeKind::Root: {
        const Color fc = Theme::orElse(th.treeFolder, th.dirtyMarker);
        ctx.fill(intersect({ix, cy - is * 0.45f, is * 0.45f, is * 0.25f}, rr), fc);
        ctx.fill(intersect({ix, cy - is * 0.25f, is * 1.2f, is * 0.7f}, rr), fc);
        break;
      }
      case TreeKind::Solution:
      case TreeKind::Project: ctx.fill(intersect({ix, cy - is / 2, is, is}, rr), th.tabAccent); break;
      case TreeKind::File: {
        const RectF f{ix + is * 0.1f, cy - is * 0.55f, is * 0.8f, is * 1.1f};
        ctx.fill(intersect({f.x, f.y, f.w, 1}, rr), dim);
        ctx.fill(intersect({f.x, f.y + f.h - 1, f.w, 1}, rr), dim);
        ctx.fill(intersect({f.x, f.y, 1, f.h}, rr), dim);
        ctx.fill(intersect({f.x + f.w - 1, f.y, 1, f.h}, rr), dim);
        break;
      }
    }
    Mark mark = Mark::None;
    if (markOf && nd.kind == TreeKind::File) mark = markOf(nd);
    Color nameColor = th.text;
    Color mc = th.dirtyMarker;
    if (mark != Mark::None) {
      mc = mark == Mark::Modified     ? Theme::orElse(th.tabVcsModified, th.dirtyMarker)
           : mark == Mark::Added      ? Theme::orElse(th.tabVcsAdded, th.vcsAdded())
           : mark == Mark::CheckedOut ? Theme::orElse(th.tabVcsCheckedOut, th.vcsDeleted())
           : mark == Mark::ReadOnly   ? Theme::orElse(th.tabVcsReadOnly, th.textDim)
                                      : th.diagnosticError;
      if (mark != Mark::ReadOnly) nameColor = mc;
    }
    const float tx = ix + is * 1.2f + cell;
    const float markW = mark != Mark::None ? 2 * cell : 0;
    const RectF clip{tx, rr.y, std::max(0.0f, rr.x + rr.w - tx - markW - 6), rr.h};
    ctx.text(tx, full.y + (full.h - ctx.lineHeight()) / 2, nd.name, nameColor, clip);
    if (mark != Mark::None) {
      const float ms = std::max(5.0f, rh * 0.3f), mx = rr.x + rr.w - 10 - ms, my = cy - ms / 2;
      if (mark == Mark::ReadOnly) {
        ctx.fill(intersect({mx, my, ms, 1}, rr), mc);
        ctx.fill(intersect({mx, my + ms - 1, ms, 1}, rr), mc);
        ctx.fill(intersect({mx, my, 1, ms}, rr), mc);
        ctx.fill(intersect({mx + ms - 1, my, 1, ms}, rr), mc);
      } else {
        ctx.fill(intersect({mx, my, ms, ms}, rr), mc);
      }
    }
  }
  if (rows_.empty() && !model_.empty()) ctx.text(list.x + pad, list.y + th.verticalPad, "No matches", th.textDim, list);

  const RectF t = thumbRect();
  if (t.h > 0) {
    ctx.fill({t.x, list.y, t.w, list.h}, th.scrollbarTrack);
    ctx.fill(t, th.scrollbarThumb);
  }
}

}  // namespace tf::ui
