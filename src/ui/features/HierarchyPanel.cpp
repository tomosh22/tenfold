#include "ui/features/HierarchyPanel.h"

#include <algorithm>
#include <cmath>

#include "core/PathUtil.h"
#include "ui/features/PickerUtil.h"

namespace tf::ui {

HierarchyPanel::Backend HierarchyPanel::editorBackend(Editor& ed) {
  Backend b;
  Editor* e = &ed;
  b.prepareCall = [e](Done<HierarchyItem> d) { e->callHierarchy(std::move(d)); };
  b.prepareType = [e](Done<HierarchyItem> d) { e->typeHierarchy(std::move(d)); };
  b.incoming = [e](const HierarchyItem& i, Done<CallEdge> d) { e->incomingCalls(i, std::move(d)); };
  b.outgoing = [e](const HierarchyItem& i, Done<CallEdge> d) { e->outgoingCalls(i, std::move(d)); };
  b.supertypes = [e](const HierarchyItem& i, Done<HierarchyItem> d) { e->supertypes(i, std::move(d)); };
  b.subtypes = [e](const HierarchyItem& i, Done<HierarchyItem> d) { e->subtypes(i, std::move(d)); };
  b.goTo = [e](const HierarchyItem& i, std::string* err) { return e->gotoHierarchyItem(i, err); };
  return b;
}

HierarchyPanel::HierarchyPanel(Editor& editor) : editor_(editor), backend_(editorBackend(editor)) {
  auto tv = std::make_unique<TreeView>();
  tree_ = tv.get();
  addChild(std::move(tv));
  tree_->onActivate = [this](int node) { activate(node); };
  tree_->onExpansionChanged = [this] { loadExpanded(); };
  tree_->onClose = [this] {
    if (onClose) onClose();
  };
  setRoot(note("Place the caret on a symbol and run View Call Hierarchy"));
}

HierarchyPanel::~HierarchyPanel() {
  tree_->onActivate = nullptr;
  tree_->onExpansionChanged = nullptr;
  tree_->onClose = nullptr;
}

std::unique_ptr<HierarchyPanel::Entry> HierarchyPanel::note(const std::string& text) {
  auto e = std::make_unique<Entry>();
  e->kind = Entry::Kind::Note;
  e->label = text;
  e->state = Entry::State::Loaded;
  return e;
}

std::string HierarchyPanel::itemLabel(const HierarchyItem& it) const {
  std::string s = it.name;
  const std::string rel = relativePath(editor_.workspace().root(), it.loc.file);
  if (!rel.empty()) s += "   " + rel + ":" + std::to_string(it.loc.pos.line + 1);
  return s;
}

std::unique_ptr<HierarchyPanel::Entry> HierarchyPanel::symbolEntry(const HierarchyItem& it, bool edge) const {
  auto e = std::make_unique<Entry>();
  e->kind = edge ? Entry::Kind::Edge : Entry::Kind::Symbol;
  e->item = it;
  e->label = itemLabel(it);
  e->state = Entry::State::Loaded;
  const bool call = mode_ == Mode::Call;
  const std::pair<Dir, std::string> groups[2] = {
      {call ? Dir::Incoming : Dir::Super, call ? "Calls To '" + it.name + "'" : "Base Types"},
      {call ? Dir::Outgoing : Dir::Sub, call ? "Calls From '" + it.name + "'" : "Derived Types"}};
  for (const auto& [dir, label] : groups) {
    auto g = std::make_unique<Entry>();
    g->kind = Entry::Kind::Group;
    g->dir = dir;
    g->item = it;
    g->label = label;
    e->kids.push_back(std::move(g));
  }
  return e;
}

std::string HierarchyPanel::title() const {
  const char* base = mode_ == Mode::Call ? "Call Hierarchy" : "Type Hierarchy";
  return rootName_.empty() ? std::string(base) : std::string(base) + ": " + rootName_;
}

std::string HierarchyPanel::status() const { return pending_ > 0 ? "Loading..." : std::string(); }

void HierarchyPanel::setRoot(std::unique_ptr<Entry> root) {
  root_ = std::move(root);
  rebuild();
}

int HierarchyPanel::addNode(TreeModel& m, int parent, Entry& e) {
  TreeKind k = TreeKind::File;
  if (e.kind == Entry::Kind::Group) k = TreeKind::Folder;
  else if (e.kind == Entry::Kind::Symbol || e.kind == Entry::Kind::Edge) k = TreeKind::Symbol;
  const int id = m.add(parent, e.label, k);
  nodes_.resize(static_cast<size_t>(id) + 1, nullptr);
  nodes_[static_cast<size_t>(id)] = &e;
  m.node(id).jumpable = e.kind == Entry::Kind::Symbol || e.kind == Entry::Kind::Edge;
  for (auto& kid : e.kids) addNode(m, id, *kid);
  // A group that was not asked yet is expandable through a placeholder.
  if (e.kids.empty() && e.state != Entry::State::Loaded && e.kind == Entry::Kind::Group) {
    auto ph = note("Loading...");
    addNode(m, id, *ph);
    e.kids.push_back(std::move(ph));
    e.kids.back()->state = Entry::State::Loaded;
  }
  return id;
}

void HierarchyPanel::rebuild() {
  // Placeholders live in `kids` while the entry has not loaded; drop them so addNode makes fresh ones.
  std::function<void(Entry&)> strip = [&](Entry& e) {
    if (e.state != Entry::State::Loaded && e.kind == Entry::Kind::Group) e.kids.clear();
    for (auto& k : e.kids) strip(*k);
  };
  if (root_) strip(*root_);
  nodes_.clear();
  TreeModel m;
  if (root_) addNode(m, -1, *root_);
  tree_->setModel(std::move(m));
  markDirty();
}

void HierarchyPanel::showMessage(Mode mode, const std::string& text) {
  ++gen_;
  pending_ = 0;
  mode_ = mode;
  rootName_.clear();
  setRoot(note(text));
}

void HierarchyPanel::open(Mode mode) {
  if (unavailable_) {
    const std::string why = unavailable_();
    if (!why.empty()) {
      showMessage(mode, "Needs clangd: " + why);
      return;
    }
  }
  ++gen_;
  mode_ = mode;
  rootName_.clear();
  pending_ = 1;
  setRoot(note("Loading..."));
  auto& prep = mode == Mode::Call ? backend_.prepareCall : backend_.prepareType;
  if (!prep) {
    pending_ = 0;
    setRoot(note("Hierarchy is not available"));
    return;
  }
  prep([this, gen = gen_, life = std::weak_ptr<int>(life_)](const std::vector<HierarchyItem>& items,
                                                            const std::string& err) {
    if (life.expired() || gen != gen_) return;  // a newer hierarchy was opened
    pending_ = 0;
    if (items.empty()) {
      setRoot(note(err.empty() ? std::string("No symbol with a hierarchy at the caret") : err));
      return;
    }
    if (items.size() == 1) {
      rootName_ = items[0].name;
      setRoot(symbolEntry(items[0], false));
    } else {  // several candidates: a heading over each of them
      auto c = std::make_unique<Entry>();
      c->kind = Entry::Kind::Note;
      c->label = mode_ == Mode::Call ? "Call Hierarchy" : "Type Hierarchy";
      c->state = Entry::State::Loaded;
      for (const HierarchyItem& it : items) c->kids.push_back(symbolEntry(it, false));
      setRoot(std::move(c));
    }
    // The first group (callers / base types) opens at once; selecting the root puts the keys on the symbol.
    if (!nodes_.empty()) {
      tree_->select(0);
      for (size_t i = 0; i < nodes_.size(); ++i)
        if (nodes_[i] && nodes_[i]->kind == Entry::Kind::Group &&
            (nodes_[i]->dir == Dir::Incoming || nodes_[i]->dir == Dir::Super)) {
          tree_->setExpanded(static_cast<int>(i), true);
          break;
        }
    }
  });
}

void HierarchyPanel::loadExpanded() {
  std::vector<Entry*> todo;
  for (size_t i = 0; i < nodes_.size(); ++i) {
    Entry* e = nodes_[i];
    if (e && e->state == Entry::State::NotLoaded && e->kind == Entry::Kind::Group &&
        tree_->isExpanded(static_cast<int>(i)))
      todo.push_back(e);
  }
  for (Entry* e : todo) load(*e);
}

void HierarchyPanel::finish(Entry& e, std::vector<std::unique_ptr<Entry>> kids, const std::string& error) {
  e.state = Entry::State::Loaded;
  e.kids = std::move(kids);
  if (e.kids.empty()) {
    if (!error.empty()) e.kids.push_back(note(error));
    else if (e.kind == Entry::Kind::Group) {
      const char* none = e.dir == Dir::Incoming   ? "No callers"
                         : e.dir == Dir::Outgoing ? "No calls"
                         : e.dir == Dir::Super    ? "No base types"
                                                  : "No derived types";
      e.kids.push_back(note(none));
    }
  }
}

void HierarchyPanel::load(Entry& e) {
  // A group asks about the symbol it hangs under, in its own direction; the answers become entries that have both
  // groups themselves (Visual Studio: each calling member expands to its own Calls To and Calls From).
  e.state = Entry::State::Loading;
  ++pending_;
  const int gen = gen_;
  Entry* ep = &e;
  auto done = [this, gen, ep, life = std::weak_ptr<int>(life_)](auto build) {
    return [this, gen, ep, life, build](const auto& items, const std::string& err) {
      if (life.expired() || gen != gen_) return;
      --pending_;
      std::vector<std::unique_ptr<Entry>> kids;
      build(items, kids);
      finish(*ep, std::move(kids), err);
      rebuild();
    };
  };
  const Dir dir = e.dir;
  const bool edges = dir == Dir::Incoming || dir == Dir::Outgoing;
  if (edges) {
    auto cb = done([this](const std::vector<CallEdge>& es, std::vector<std::unique_ptr<Entry>>& out) {
      for (const CallEdge& c : es) {
        auto k = symbolEntry(c.item, true);
        k->calls = c.ranges.size();
        if (k->calls > 1) k->label += "  (" + std::to_string(k->calls) + " calls)";
        out.push_back(std::move(k));
      }
    });
    auto& fn = dir == Dir::Incoming ? backend_.incoming : backend_.outgoing;
    if (fn) fn(e.item, cb);
    else {
      --pending_;
      finish(e, {}, "not available");
      rebuild();
    }
  } else {
    auto cb = done([this](const std::vector<HierarchyItem>& is, std::vector<std::unique_ptr<Entry>>& out) {
      for (const HierarchyItem& it : is) out.push_back(symbolEntry(it, true));
    });
    auto& fn = dir == Dir::Super ? backend_.supertypes : backend_.subtypes;
    if (fn) fn(e.item, cb);
    else {
      --pending_;
      finish(e, {}, "not available");
      rebuild();
    }
  }
}

const HierarchyItem* HierarchyPanel::itemOf(int node) const {
  if (node < 0 || static_cast<size_t>(node) >= nodes_.size() || !nodes_[static_cast<size_t>(node)]) return nullptr;
  const Entry* e = nodes_[static_cast<size_t>(node)];
  return e->kind == Entry::Kind::Symbol || e->kind == Entry::Kind::Edge ? &e->item : nullptr;
}

bool HierarchyPanel::activate(int node) {
  const HierarchyItem* it = itemOf(node);
  if (!it || !backend_.goTo) return false;
  std::string err;
  if (!backend_.goTo(*it, &err)) {
    if (onMessage) onMessage(err.empty() ? "Cannot open " + it->name : err);
    return false;
  }
  if (onJumped) onJumped();
  return true;
}

std::vector<std::string> HierarchyPanel::rowLabels() const {
  std::vector<std::string> out;
  for (const TreeView::Row& r : tree_->rows()) out.push_back(tree_->model().node(r.node).name);
  return out;
}

// ---- layout, painting, input ----

float HierarchyPanel::headerHeight() const {
  return (root() ? root()->text().lineHeight() : 16.0f) + 2 * theme().verticalPad;
}
RectF HierarchyPanel::headerRect() const { return {bounds().x, bounds().y, bounds().w, headerHeight()}; }
RectF HierarchyPanel::closeButtonRect() const {
  const Theme& th = theme();
  const RectF h = headerRect();
  const float w = (root() ? root()->text().measure("\xC3\x97") : 8.0f) + 2 * th.padding;
  return {h.x + h.w - th.padding / 2 - w, h.y + 2, w, h.h - 4};
}

void HierarchyPanel::onLayout() {
  const float hh = headerHeight();
  tree_->layout({bounds().x, bounds().y + hh, bounds().w, std::max(0.0f, bounds().h - hh)});
}

void HierarchyPanel::paint(PaintContext& ctx) {
  const Theme& th = ctx.theme;
  ctx.fill(bounds(), th.panelBackground);
  const RectF h = headerRect();
  ctx.fill(h, th.tabStripBackground);
  const RectF cb = closeButtonRect();
  const float ty = h.y + th.verticalPad;
  std::string t = title();
  const std::string st = status();
  if (!st.empty()) t += "   " + st;
  ctx.text(h.x + th.padding, ty, t, th.text, {h.x, h.y, std::max(0.0f, cb.x - h.x - th.padding), h.h});
  ctx.fill(cb, closeHot_ ? th.tabHover : th.tabInactive);
  ctx.text(cb.x + th.padding, ty, "\xC3\x97", th.text, cb);
}

bool HierarchyPanel::onEvent(const InputEvent& e) {
  if (e.type == InputType::MouseMove) {
    const bool hot = ui::contains(closeButtonRect(), e.x, e.y);
    if (hot != closeHot_) {
      closeHot_ = hot;
      markDirty();
    }
    return false;
  }
  if (e.type == InputType::MouseDown && e.button == MouseButton::Left && ui::contains(closeButtonRect(), e.x, e.y)) {
    if (onClose) onClose();
    return true;
  }
  return false;
}

}  // namespace tf::ui
