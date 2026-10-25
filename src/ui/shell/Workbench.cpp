#include "ui/shell/Workbench.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "ui/shell/DocumentView.h"

namespace tf::ui {

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {
constexpr int kMaxDepth = 12, kMaxPanels = 16, kMaxTabs = 256;
bool isHorizontal(Splitter::Orientation o) { return o == Splitter::Orientation::Horizontal; }
// Tolerant getters: layout.json is user-editable, a wrong type must never throw.
bool getBool(const Json& j, const char* k, bool def) {
  return j.contains(k) && j[k].is_boolean() ? j[k].get<bool>() : def;
}
int getInt(const Json& j, const char* k, int def) {
  return j.contains(k) && j[k].is_number_integer() ? j[k].get<int>() : def;
}
}  // namespace

// Last child of the workbench: the active-panel accent line, and while a tab is dragged the drop highlight. It only
// covers the whole window during a drag (the mouse is captured then), so it never steals clicks.
class Workbench::Overlay : public Widget {
 public:
  explicit Overlay(Workbench& w) : wb_(w) {}
  RectF accent{0, 0, 0, 0};

 protected:
  void paint(PaintContext& ctx) override {
    if (accent.w > 0) ctx.fill(accent, ctx.theme.tabAccent);
    if (wb_.dragTab_ >= 0 && wb_.drop_.kind != Drop::None) {
      Color c = ctx.theme.tabInsert;
      c.a = 0.30f;
      ctx.fill(wb_.drop_.rect, c);
    }
  }

 private:
  Workbench& wb_;
};

Workbench::Workbench(gfx::TextRenderer& text, Editor& editor, const Theme& theme)
    : RootWidget(text, theme), editor_(editor) {
  auto sb = std::make_unique<StatusBar>();
  status_ = sb.get();
  addChild(std::move(sb));
  auto ov = std::make_unique<Overlay>(*this);
  overlay_ = ov.get();
  addChild(std::move(ov));
  status_->addSegment("workspace", StatusBar::Align::Left);
  status_->addSegment("pos", StatusBar::Align::Left);
  status_->addSegment("clangd", StatusBar::Align::Left);
  status_->addSegment("fastindex", StatusBar::Align::Left);
  status_->addSegment("encoding", StatusBar::Align::Right);
  status_->addSegment("eol", StatusBar::Align::Right);
  status_->setText("workspace", editor_.workspace().root().filename().string());
  installTree(makePanel());
}

Workbench::~Workbench() = default;

// ---------------------------------------------------------------------------------------------------------------
// Tree plumbing
// ---------------------------------------------------------------------------------------------------------------

std::unique_ptr<Panel> Workbench::makePanel() {
  auto p = std::make_unique<Panel>();
  Panel* raw = p.get();
  raw->onActivated = [this, raw](int) {
    setActivePanel(raw);
    cycle_ = {};
  };
  raw->onCloseRequested = [this](int id) { closeTab(id); };
  raw->tabs().onBeginDragOut = [this](int id, float x, float y) { beginTabDrag(id, x, y); };
  return p;
}

void Workbench::collect(Widget* w, std::vector<Panel*>& out) const {
  if (auto* p = dynamic_cast<Panel*>(w)) out.push_back(p);
  else if (auto* s = dynamic_cast<Splitter*>(w)) {
    collect(s->first(), out);
    collect(s->second(), out);
  }
}

std::vector<Panel*> Workbench::panels() const {
  std::vector<Panel*> out;
  if (top_) collect(top_, out);
  return out;
}

void Workbench::installTree(std::unique_ptr<Widget> top, bool keepTabs) {
  if (top_) removeChild(top_);
  if (!keepTabs) tabs_.clear();
  cycle_ = {};
  top_ = addChild(std::move(top));
  addChild(removeChild(overlay_));  // the overlay stays last (on top)
  auto ps = panels();
  active_ = ps.empty() ? nullptr : ps.front();
  requestLayout();
}

std::unique_ptr<Widget> Workbench::replaceNode(Widget* old, std::unique_ptr<Widget> nw) {
  Widget* par = old->parent();
  if (auto* s = dynamic_cast<Splitter*>(par)) return s->replaceChild(old, std::move(nw));
  auto out = removeChild(old);
  top_ = addChild(std::move(nw));
  addChild(removeChild(overlay_));
  return out;
}

void Workbench::setTopBar(Widget* bar, float height) {
  topBar_ = bar;
  topBarH_ = height;
  requestLayout();
}

void Workbench::onLayout() {
  const RectF& b = bounds();
  const float sh = status_->preferredHeight();
  const float tb = topBar_ ? topBarH_ : 0;
  if (topBar_) topBar_->layout({b.x, b.y, b.w, tb});
  if (top_) top_->layout({b.x, b.y + tb, b.w, b.h - sh - tb});
  status_->layout({b.x, b.y + b.h - sh, b.w, sh});
  RectF acc{0, 0, 0, 0};
  if (active_ && panels().size() > 1)
    acc = {active_->bounds().x, active_->bounds().y, active_->bounds().w, theme().accentHeight};
  overlay_->accent = acc;
  overlay_->layout(dragTab_ >= 0 ? b : acc);
}

Panel* Workbench::splitPanel(Panel* target, Splitter::Orientation o, bool after, int moveId) {
  if (!target) return nullptr;
  auto npOwn = makePanel();
  Panel* np = npOwn.get();
  auto spl = std::make_unique<Splitter>(o);
  Splitter* sraw = spl.get();
  const int activeId = target->activeId();
  Document* dup = moveId < 0 && activeId >= 0 ? docOf(activeId) : nullptr;
  auto old = replaceNode(target, std::move(spl));
  if (after) sraw->setChildren(std::move(old), std::move(npOwn));
  else sraw->setChildren(std::move(npOwn), std::move(old));
  if (moveId >= 0) moveTab(moveId, np, -1);
  else if (dup) addTabTo(np, *dup, false);
  setActivePanel(np);
  requestLayout();
  return np;
}

bool Workbench::closePanel(Panel* p) {
  auto* s = dynamic_cast<Splitter*>(p->parent());
  if (!s) return false;
  Widget* sib = s->first() == p ? s->second() : s->first();
  std::vector<Panel*> inSib;
  collect(sib, inSib);
  Panel* n = inSib.front();
  const int keep = n->activeId();
  std::vector<int> ids;
  for (const auto& t : p->tabs().tabs()) ids.push_back(t.id);
  for (int id : ids) moveTabImpl(id, n, -1);
  if (keep >= 0) n->setActive(keep);
  auto sibOwn = s->removeChild(sib);
  auto dead = replaceNode(s, std::move(sibOwn));  // destroys the splitter and with it the (now empty) panel
  dead.reset();
  if (active_ == p || !active_) active_ = n;
  requestLayout();
  showActiveTab();
  return true;
}

void Workbench::setActivePanel(Panel* p) {
  if (!p) return;
  if (active_ != p) {
    active_ = p;
    requestLayout();
  }
  showActiveTab();
}

// ---------------------------------------------------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------------------------------------------------

Document* Workbench::docOf(int id) const {
  auto it = tabs_.find(id);
  return it == tabs_.end() ? nullptr : editor_.document(it->second.path);
}

fs::path Workbench::tabPath(int id) const {
  auto it = tabs_.find(id);
  return it == tabs_.end() ? fs::path() : it->second.path;
}

Panel* Workbench::panelOf(int id) const {
  for (Panel* p : panels())
    if (p->content(id)) return p;
  return nullptr;
}

int Workbench::findTab(Panel* p, const Document& doc) const {
  for (const auto& t : p->tabs().tabs()) {
    auto it = tabs_.find(t.id);
    if (it != tabs_.end() && it->second.path == doc.path()) return t.id;
  }
  return -1;
}

int Workbench::addTabTo(Panel* p, Document& doc, bool pinned) {
  const int id = nextId_++;
  tabs_[id] = {doc.path()};
  p->addTab(id, doc.path().filename().string(), makeDocumentView(editor_, doc), pinned);
  p->tabs().setDirty(id, doc.dirty());
  return id;
}

int Workbench::openFile(const fs::path& path, Panel* in) {
  Document* d = editor_.open(path);
  if (!d) return -1;
  Panel* p = in ? in : active_;
  int id = findTab(p, *d);
  if (id < 0) id = addTabTo(p, *d, false);
  else p->setActive(id);
  setActivePanel(p);
  return id;
}

void Workbench::releaseDocument(const fs::path& path) {
  for (const auto& [id, t] : tabs_)
    if (t.path == path) return;
  Document* d = editor_.document(path);
  if (d && !d->dirty()) editor_.close(path);
  lastCur_ = editor_.currentDocument();
}

bool Workbench::closeTab(int id) {
  Panel* p = panelOf(id);
  if (!p) return false;
  const fs::path path = tabs_[id].path;
  p->removeTab(id);
  tabs_.erase(id);
  cycle_ = {};
  releaseDocument(path);
  if (p == active_) showActiveTab();
  requestLayout();
  return true;
}

void Workbench::closeOthers(int id) {
  Panel* p = panelOf(id);
  if (!p) return;
  std::vector<int> kill;
  for (const auto& t : p->tabs().tabs())
    if (t.id != id && !t.pinned) kill.push_back(t.id);
  for (int k : kill) closeTab(k);
}

void Workbench::closeAllButPinned(Panel* p) {
  std::vector<int> kill;
  for (const auto& t : p->tabs().tabs())
    if (!t.pinned) kill.push_back(t.id);
  for (int k : kill) closeTab(k);
}

void Workbench::setPinned(int id, bool pinned) {
  if (Panel* p = panelOf(id)) p->tabs().setPinned(id, pinned);
}

bool Workbench::moveTab(int id, Panel* dst, int index) {
  Panel* src = panelOf(id);
  if (!moveTabImpl(id, dst, index)) return false;
  if (src && src != dst && src->tabs().tabs().empty()) closePanel(src);  // emptied by the move: the panel goes away
  setActivePanel(dst);
  return true;
}

bool Workbench::moveTabImpl(int id, Panel* dst, int index) {
  Panel* src = panelOf(id);
  if (!src || !dst) return false;
  if (src == dst) {
    if (index >= 0) dst->tabs().moveTab(id, index);
    return true;
  }
  const int ti = src->tabs().indexOf(id);
  const TabStrip::Tab info = src->tabs().tabs()[static_cast<size_t>(ti)];
  Document* d = docOf(id);
  const int existing = d ? findTab(dst, *d) : -1;
  if (existing >= 0) {  // dst already shows this file: fold the moved tab into it
    closeTab(id);
    dst->setActive(existing);
  } else {
    auto content = src->removeTab(id);
    dst->addTab(id, info.title, std::move(content), info.pinned);
    dst->tabs().setDirty(id, info.dirty);
    if (index >= 0) dst->tabs().moveTab(id, index);
  }
  cycle_ = {};
  requestLayout();
  return true;
}

int Workbench::cycleMru(Panel* p, bool forward) {
  TabStrip& st = p->tabs();
  const auto& mru = st.mruOrder();
  if (mru.size() < 2) return st.activeId();
  if (cycle_.panel != p || cycle_.expect != st.activeId() || cycle_.order.empty()) cycle_ = {p, mru, 0, st.activeId()};
  const size_t n = cycle_.order.size();
  for (size_t k = 0; k < n; ++k) {
    cycle_.pos = (cycle_.pos + (forward ? 1 : n - 1)) % n;
    const int id = cycle_.order[cycle_.pos];
    if (st.indexOf(id) < 0) continue;  // closed during the cycle
    p->setActive(id);
    cycle_.expect = id;
    if (p == active_) showActiveTab();
    break;
  }
  return st.activeId();
}

// ---------------------------------------------------------------------------------------------------------------
// Drag between panels
// ---------------------------------------------------------------------------------------------------------------

void Workbench::beginTabDrag(int id, float x, float y) {
  if (!panelOf(id)) return;
  dragTab_ = id;
  captureMouse();
  drop_ = dropTargetAt(x, y);
  requestLayout();
}

void Workbench::cancelDrag() {
  dragTab_ = -1;
  drop_ = {};
  if (hasCapture()) releaseMouse();
  requestLayout();
}

Workbench::DropTarget Workbench::dropTargetAt(float x, float y) const {
  for (Panel* p : panels()) {
    TabStrip& st = p->tabs();
    if (ui::contains(st.bounds(), x, y)) {
      int idx = 0;
      for (const auto& t : st.tabs()) {
        const RectF r = st.tabRect(t.id);
        if (x > r.x + r.w / 2) ++idx;
      }
      return {p, Drop::Strip, idx, st.bounds()};
    }
    const RectF c = p->contentRect();
    if (!ui::contains(c, x, y)) continue;
    const float fx = (x - c.x) / c.w, fy = (y - c.y) / c.h;
    if (fx < 0.25f) return {p, Drop::Left, -1, {c.x, c.y, c.w / 2, c.h}};
    if (fx > 0.75f) return {p, Drop::Right, -1, {c.x + c.w / 2, c.y, c.w / 2, c.h}};
    if (fy < 0.25f) return {p, Drop::Top, -1, {c.x, c.y, c.w, c.h / 2}};
    if (fy > 0.75f) return {p, Drop::Bottom, -1, {c.x, c.y + c.h / 2, c.w, c.h / 2}};
    return {p, Drop::Center, -1, c};
  }
  return {};
}

void Workbench::applyDrop(int id, const DropTarget& t) {
  if (!t.panel || !panelOf(id)) return;
  const bool lone = panelOf(id) == t.panel && t.panel->tabs().tabs().size() == 1;
  switch (t.kind) {
    case Drop::Strip:
      moveTab(id, t.panel, t.index > t.panel->tabs().indexOf(id) && panelOf(id) == t.panel ? t.index - 1 : t.index);
      break;
    case Drop::Center: moveTab(id, t.panel, -1); break;
    case Drop::Left:
    case Drop::Right:
      if (!lone) splitPanel(t.panel, Splitter::Orientation::Horizontal, t.kind == Drop::Right, id);
      break;
    case Drop::Top:
    case Drop::Bottom:
      if (!lone) splitPanel(t.panel, Splitter::Orientation::Vertical, t.kind == Drop::Bottom, id);
      break;
    case Drop::None: break;
  }
}

bool Workbench::onEvent(const InputEvent& e) {
  if (dragTab_ < 0) return false;
  if (e.type == InputType::MouseMove) {
    drop_ = dropTargetAt(e.x, e.y);
    markDirty();
    return true;
  }
  if (e.type == InputType::MouseUp) {
    const int id = dragTab_;
    const DropTarget t = dropTargetAt(e.x, e.y);
    cancelDrag();
    applyDrop(id, t);
    return true;
  }
  return false;
}

bool Workbench::dispatch(const InputEvent& e) {
  if (e.type == InputType::MouseDown && !captured())
    for (Widget* w = hitTest(e.x, e.y); w; w = w->parent())
      if (auto* p = dynamic_cast<Panel*>(w)) {
        setActivePanel(p);
        break;
      }
  return RootWidget::dispatch(e);
}

// ---------------------------------------------------------------------------------------------------------------
// Editor sync
// ---------------------------------------------------------------------------------------------------------------

void Workbench::showDocument(Document& d) {
  if (editor_.currentDocument() != &d) {
    if (Document* cur = editor_.currentDocument()) lastCaret_[cur->path().generic_string()] = editor_.cursor().pos;
    auto it = lastCaret_.find(d.path().generic_string());
    editor_.activateDocument(d, it == lastCaret_.end() ? Position{} : it->second);
  }
  lastCur_ = &d;
}

void Workbench::showActiveTab() {
  if (!active_ || active_->activeId() < 0) return;
  if (Document* d = docOf(active_->activeId())) showDocument(*d);
  focusActiveView();
}

// Keys go to the focused widget: after a tab/panel switch that must be the view now shown (the old one is hidden).
void Workbench::focusActiveView() {
  if (!active_) return;
  Widget* w = active_->activeContent();
  if (w && w->acceptsFocus() && !w->hasFocus()) w->setFocus();
}

// The Editor navigated: make a tab for its current document visible without recording anything.
bool Workbench::revealDocument(Document& d) {
  int id = active_ ? findTab(active_, d) : -1;
  Panel* p = active_;
  if (id < 0)
    for (Panel* q : panels())
      if ((id = findTab(q, d)) >= 0) {
        p = q;
        break;
      }
  if (id < 0) id = addTabTo(p, d, false);
  else p->setActive(id);
  if (p != active_) {
    active_ = p;
    requestLayout();
  }
  focusActiveView();
  return true;
}

void Workbench::sync() {
  Document* cur = editor_.currentDocument();
  if (cur != lastCur_) {
    lastCur_ = cur;
    if (cur) revealDocument(*cur);
  }
  if (cur) lastCaret_[cur->path().generic_string()] = editor_.cursor().pos;
  for (Panel* p : panels())
    for (const auto& t : p->tabs().tabs())
      if (Document* d = docOf(t.id)) p->tabs().setDirty(t.id, d->dirty());
  refreshStatus();
}

void Workbench::refreshStatus() {
  Document* cur = editor_.currentDocument();
  if (cur) {
    const Position p = editor_.cursor().pos;
    status_->setText("pos", "Ln " + std::to_string(p.line + 1) + ", Col " + std::to_string(p.column + 1));
    status_->setText("eol", cur->buffer().lineEnding() == LineEnding::CRLF ? "CRLF" : "LF");
    status_->setText("encoding", "UTF-8");
  } else {
    for (const char* k : {"pos", "eol", "encoding"}) status_->setText(k, "");
  }
  const SemanticEngine::Status st = editor_.started() ? editor_.semantic().status() : SemanticEngine::Status{};
  std::string clangd = "clangd: off";
  if (st.clangdRunning) {
    if (!st.clangdInitialized) clangd = "clangd: starting";
    else if (st.clangdIndex.active)
      clangd = "clangd: indexing" +
               (st.clangdIndex.percentage >= 0 ? " " + std::to_string(st.clangdIndex.percentage) + "%" : "");
    else clangd = "clangd: ready";
  }
  status_->setText("clangd", clangd);
  status_->setText("fastindex", st.fastIndexDone ? "fast index: " + std::to_string(st.fastFiles) + " files"
                                                 : "fast index: indexing");
}

// ---------------------------------------------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------------------------------------------

fs::path Workbench::layoutPath() const { return editor_.workspace().root() / ".tenfold" / "layout.json"; }

std::string Workbench::layoutJson() const {
  const fs::path root = editor_.workspace().root();
  auto rel = [&](const fs::path& p) {
    fs::path r = p.is_absolute() ? p.lexically_relative(root) : p;
    return (r.empty() || *r.begin() == "..") ? p.generic_string() : r.generic_string();
  };
  const auto ps = panels();
  auto node = [&](auto&& self, Widget* w) -> Json {
    if (auto* s = dynamic_cast<Splitter*>(w))
      return {{"type", "split"},
              {"orientation", isHorizontal(s->orientation()) ? "horizontal" : "vertical"},
              {"ratio", s->ratio()},
              {"first", self(self, s->first())},
              {"second", self(self, s->second())}};
    auto* p = static_cast<Panel*>(w);
    Json tabs = Json::array();
    int activeIdx = -1, i = 0;
    for (const auto& t : p->tabs().tabs()) {
      tabs.push_back({{"path", rel(tabPath(t.id))}, {"pinned", t.pinned}});
      if (t.id == p->activeId()) activeIdx = i;
      ++i;
    }
    return {{"type", "panel"}, {"tabs", tabs}, {"active", activeIdx}};
  };
  const auto it = std::find(ps.begin(), ps.end(), active_);
  return Json{{"version", 1}, {"activePanel", static_cast<int>(it - ps.begin())}, {"root", node(node, top_)}}.dump(2);
}

bool Workbench::saveLayout() const {
  std::error_code ec;
  fs::create_directories(layoutPath().parent_path(), ec);
  std::ofstream out(layoutPath(), std::ios::binary | std::ios::trunc);
  out << layoutJson() << "\n";
  return static_cast<bool>(out);
}

bool Workbench::restoreLayout() {
  std::ifstream in(layoutPath(), std::ios::binary);
  if (!in) return false;
  std::stringstream ss;
  ss << in.rdbuf();
  return restoreLayoutFromJson(ss.str());
}

bool Workbench::restoreLayoutFromJson(const std::string& text) {
  Json j = Json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object() || !j.contains("root") || !j["root"].is_object()) return false;
  // Validate the shape first so a corrupt file never leaves a half-built tree behind.
  int panelCount = 0;
  auto valid = [&](auto&& self, const Json& n, int depth) -> bool {
    if (depth > kMaxDepth || !n.is_object() || !n.contains("type") || !n["type"].is_string()) return false;
    const std::string t = n["type"];
    if (t == "panel") {
      if (++panelCount > kMaxPanels) return false;
      if (!n.contains("tabs") || !n["tabs"].is_array() || n["tabs"].size() > kMaxTabs) return false;
      for (const Json& tab : n["tabs"])
        if (!tab.is_object() || !tab.contains("path") || !tab["path"].is_string()) return false;
      return true;
    }
    if (t != "split" || !n.contains("first") || !n.contains("second")) return false;
    return self(self, n["first"], depth + 1) && self(self, n["second"], depth + 1);
  };
  if (!valid(valid, j["root"], 0)) return false;

  installTree(makePanel());  // drop the old layout (and its tabs) first
  std::vector<Panel*> built;
  auto build = [&](auto&& self, const Json& n) -> std::unique_ptr<Widget> {
    if (n["type"] == "split") {
      const bool horiz = !(n.contains("orientation") && n["orientation"] == "vertical");
      auto s = std::make_unique<Splitter>(horiz ? Splitter::Orientation::Horizontal : Splitter::Orientation::Vertical);
      auto a = self(self, n["first"]);
      auto b = self(self, n["second"]);
      s->setChildren(std::move(a), std::move(b));
      if (n.contains("ratio") && n["ratio"].is_number()) s->setRatio(std::clamp(n["ratio"].get<float>(), 0.05f, 0.95f));
      return s;
    }
    auto own = makePanel();
    Panel* p = own.get();
    built.push_back(p);
    int activeId = -1;
    const int want = getInt(n, "active", -1);
    int i = 0;
    for (const Json& tab : n["tabs"]) {
      const bool pinned = getBool(tab, "pinned", false);
      Document* d = editor_.open(fs::path(tab["path"].get<std::string>()));  // missing files are dropped
      if (d && findTab(p, *d) < 0) {
        const int id = addTabTo(p, *d, pinned);
        if (i == want) activeId = id;
      }
      ++i;
    }
    if (activeId >= 0) p->setActive(activeId);
    return own;
  };
  installTree(build(build, j["root"]), true);
  const int ap = getInt(j, "activePanel", 0);
  active_ = built.empty() ? nullptr : built[static_cast<size_t>(std::clamp(ap, 0, static_cast<int>(built.size()) - 1))];
  if (!active_) active_ = panels().front();
  showActiveTab();
  return true;
}

}  // namespace tf::ui
