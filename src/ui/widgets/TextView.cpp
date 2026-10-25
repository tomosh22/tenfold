#include "ui/widgets/TextView.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace tf::ui {

namespace {
Color mix(Color a, Color b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1};
}
constexpr int kBracketLines = 1000;  // how far a bracket search looks
}  // namespace

TextView::TextView(Editor& editor, Document* pinned) : editor_(editor), pinned_(pinned) {
  auto sb = std::make_unique<Scrollbar>();
  sb_ = sb.get();
  sb_->onScroll = [this](float y, bool animate) {
    vp_.scrollTo(vp_.targetX(), std::clamp(y, 0.0f, maxScrollY()), animate && settings_.smoothScroll);
    markDirty();
  };
  addChild(std::move(sb));
}

TextView::~TextView() { bindListener(nullptr); }

void TextView::bindDocument() {
  Document* d = document();
  if (d == listenDoc_ && hl_.attached(d ? &d->buffer() : nullptr)) return;
  bindListener(d);
  hl_.attach(d ? &d->buffer() : nullptr);
  wrap_.reset();
  anchorLine_ = -1;
  prefSel_.clear();
}

// Keeps one change listener on the bound document that feeds the highlighter. The document may have been closed
// since (a view that follows the current document); the Editor's lookup by path tells.
void TextView::bindListener(Document* d) {
  if (listenDoc_ && editor_.document(listenPath_) == listenDoc_) listenDoc_->removeListener(listenId_);
  listenDoc_ = d;
  if (!d) return;
  listenPath_ = d->path();
  listenId_ = d->addListener([this](Document&, const Document::Change& c) {
    hl_.onChange(c);
    wrap_.onChange(c);
  });
}

bool TextView::colouring() const {
  const Document* d = document();
  if (!d) return false;
  std::string ext = d->path().extension().string();
  for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (const char* e : {".c", ".cc", ".cpp", ".cxx", ".c++", ".h", ".hh", ".hpp", ".hxx", ".h++", ".inl", ".ipp",
                        ".tpp", ".cu", ".cuh", ".ino", ".mm"})
    if (ext == e) return true;
  return false;
}

float TextView::scrollbarWidth() const { return root() ? std::round(root()->text().cellWidth() * 1.5f) : 0.0f; }

void TextView::applySettings(const EditorSettings& s) {
  settings_.smoothScroll = s.smoothScroll;
  settings_.caretBlink = s.caretBlink;
  settings_.caretBlinkMs = s.caretBlinkMs;
  settings_.highlightCurrentLine = s.highlightCurrentLine;
  settings_.wheelLines = s.wheelLines;
  settings_.tabWidth = s.tabWidth;
  settings_.columnGuide = s.columnGuide;
  settings_.showWhitespace = s.showWhitespace;
  if (root()) root()->text().setTabWidth(s.tabWidth);
  setWordWrap(s.wordWrap);
  markDirty();
}

void TextView::setSemanticTokens(std::vector<TokenSpan> spans, const LspSemanticTokensLegend& legend) {
  hl_.setSemanticTokens(std::move(spans), legend.types, legend.modifiers);
  markDirty();
}

void TextView::setSearchMarkers(const std::vector<Range>& matches) {
  searchLines_.clear();
  searchLines_.reserve(matches.size());
  for (const Range& r : matches) searchLines_.push_back(r.start.line);
  updateSearchLane();
}

void TextView::setFindMatches(std::vector<Range> matches, int current) {
  findMatches_ = std::move(matches);
  findCurrent_ = current;
  updateSearchLane();
  markDirty();
}

void TextView::updateSearchLane() {
  if (findMatches_.empty()) {
    setMarkLines(Scrollbar::Lane::Search, searchLines_);
    return;
  }
  std::vector<int> lines;
  lines.reserve(findMatches_.size());
  for (const Range& r : findMatches_) lines.push_back(r.start.line);
  setMarkLines(Scrollbar::Lane::Search, std::move(lines));
}

uint64_t TextView::diagnosticSignature() const {
  const Document* d = document();
  uint64_t h = 1469598103934665603ull;
  if (!d) return h;
  for (const auto* ds : {&editor_.diagnostics(d->path()), &editor_.buildDiagnostics(d->path())})
    for (const Diagnostic& g : *ds)
      for (int v : {g.range.start.line, g.range.start.column, g.range.end.line, g.range.end.column, g.severity})
        h = (h ^ static_cast<uint64_t>(static_cast<uint32_t>(v))) * 1099511628211ull;
  return h;
}

void TextView::updateDiagMarkers() {
  std::vector<int> lanes[5];
  if (const Document* d = document()) {
    for (const Diagnostic& g : editor_.diagnostics(d->path()))
      lanes[g.severity <= 1 ? 0 : g.severity == 2 ? 1 : 2].push_back(g.range.start.line);
    for (const Diagnostic& g : editor_.buildDiagnostics(d->path()))  // notes get no mark, like no squiggle
      if (g.severity <= 2) lanes[g.severity <= 1 ? 3 : 4].push_back(g.range.start.line);
  }
  setMarkLines(Scrollbar::Lane::Error, std::move(lanes[0]));
  setMarkLines(Scrollbar::Lane::Warning, std::move(lanes[1]));
  setMarkLines(Scrollbar::Lane::Info, std::move(lanes[2]));
  setMarkLines(Scrollbar::Lane::BuildError, std::move(lanes[3]));
  setMarkLines(Scrollbar::Lane::BuildWarning, std::move(lanes[4]));
}

Layout TextView::geometry() const { return Layout(root()->text().textLayout()); }

// ---- soft wrap (B2c) ----

// Wrap width in cells: the text area minus one cell, which keeps the caret at the end of a full row (and one hanging
// space) inside the view. 0 = wrapping off.
int TextView::wrapCols() const {
  if (!settings_.wordWrap || !root() || !document() || bounds().w <= 0) return 0;
  const Layout L = geometry();
  const float visW = bounds().w - scrollbarWidth() - L.textOffset(document()->buffer().lineCount());
  return std::max(1, static_cast<int>(std::floor(visW / L.cellWidth())) - 1);
}

WrapLayout& TextView::wrapLayout() const {
  const Document* d = document();
  const bool ok = d && root();
  wrap_.bind(ok ? &d->buffer() : nullptr, ok ? &root()->text().textLayout() : nullptr, ok ? wrapCols() : 0);
  return wrap_;
}

bool TextView::wordWrapActive() const { return wrapLayout().enabled(); }

float TextView::scrollX() const { return wordWrapActive() ? 0.0f : std::floor(vp_.x()); }

float TextView::totalHeight() const {
  return root() ? static_cast<float>(wrapLayout().totalRows()) * root()->text().lineHeight() : 0.0f;
}

// Rebinds the wrap map to the current width. When the wrap width changed (resize, toggle, a wider gutter) the first
// visible line stays where it is: every cached row count is gone, so lines above it count one row again.
void TextView::syncWrap() {
  if (!root() || !document()) return;
  WrapLayout& W = wrapLayout();
  const int cols = W.enabled() ? W.cols() : 0;
  if (cols == syncedCols_) return;
  const bool first = syncedCols_ < 0;
  syncedCols_ = cols;
  float y = vp_.y();
  if (!first && anchorLine_ >= 0)
    y = static_cast<float>(W.firstRowOf(std::min(anchorLine_, document()->buffer().lineCount() - 1))) *
        root()->text().lineHeight();
  vp_.jumpTo(cols > 0 ? 0.0f : vp_.x(), std::clamp(y, 0.0f, maxScrollY()));
  markDirty();
}

void TextView::setWordWrap(bool on) {
  if (settings_.wordWrap == on) return;
  settings_.wordWrap = on;
  syncWrap();
  markDirty();
}

// The visible rows [floor(sy / lh), floor((sy + h) / lh)] as row references. Lines on screen are examined here, so
// rows of lines nobody had looked at yet appear (and the scroll range grows) as they come into view.
void TextView::collectVisibleRows(std::vector<RowRef>& out) const {
  out.clear();
  const Document* d = document();
  if (!d || !root()) return;
  WrapLayout& W = wrapLayout();
  const float lh = root()->text().lineHeight(), sy = std::floor(vp_.y());
  const int total = W.totalRows();
  const int vr0 = std::clamp(static_cast<int>(std::floor(sy / lh)), 0, total);
  const int vr1 = std::clamp(static_cast<int>(std::floor((sy + bounds().h) / lh)) + 1, vr0, total);
  if (vr0 >= vr1) return;
  int r0 = 0;
  int line = W.lineOfRow(vr0, &r0);
  const int n = d->buffer().lineCount();
  int vrow = vr0;
  for (bool firstLine = true; line < n && vrow < vr1; ++line, firstLine = false) {
    for (int r = firstLine ? r0 : 0; vrow < vr1; ++r) {
      const int end = W.rowEnd(line, r);  // scans, so the row count below is current
      if (r >= W.rowsEstimate(line)) break;
      out.push_back({line, r, vrow, W.rowStart(line, r), end, W.rowIndent(line, r)});
      ++vrow;
    }
  }
}

std::pair<size_t, size_t> TextView::visibleRange(int line) const {
  const auto lo = std::lower_bound(vis_.begin(), vis_.end(), line, [](const RowRef& r, int l) { return r.line < l; });
  auto hi = lo;
  while (hi != vis_.end() && hi->line == line) ++hi;
  return {static_cast<size_t>(lo - vis_.begin()), static_cast<size_t>(hi - vis_.begin())};
}

template <class Fn>
void TextView::forSegments(int line, int b0, int b1, bool withBreak, Fn fn) const {
  const auto [lo, hi] = visibleRange(line);
  const int n = static_cast<int>(document()->buffer().line(line).size());
  const float cw = root()->text().cellWidth();
  for (size_t i = lo; i < hi; ++i) {
    const RowRef& r = vis_[i];
    const bool last = r.end >= n;
    const int a = std::max(b0, r.start), e = std::min(b1, r.end);
    if (a > e) continue;
    if (a == e) {
      if (b0 < b1 && !(last && withBreak && e == n)) continue;  // only touches the row's edge
      if (b0 == b1 && !last && a == r.end) continue;            // the caret position belongs to the next row
    }
    const float x0 = wrap_.xOf(line, r.row, a);
    float x1 = wrap_.xOf(line, r.row, e);
    if (last && withBreak) x1 += cw;
    fn(r, x0, x1);
  }
}

// Scrollbar markers come in as document lines; the bar works in visual rows, so they are converted (again whenever
// the wrap map changed).
void TextView::setMarkLines(Scrollbar::Lane lane, std::vector<int> lines) {
  markLines_[static_cast<int>(lane)] = lines;
  if (!wrap_.enabled()) {
    sb_->setMarkers(lane, std::move(lines));
    return;
  }
  for (int& l : lines) l = wrap_.firstRowOf(l);
  sb_->setMarkers(lane, std::move(lines));
}

void TextView::pushMarkers() {
  markGen_ = wrap_.generation();
  for (int lane = 0; lane < 7; ++lane) setMarkLines(static_cast<Scrollbar::Lane>(lane), markLines_[lane]);
}

int64_t TextView::now() const {
  if (settings_.clock) return settings_.clock();
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

RectF TextView::textArea() const {
  const Layout L = geometry();
  const Document* d = document();
  const float gw = L.gutterWidth(d ? d->buffer().lineCount() : 1);
  return intersect({bounds().x + gw, bounds().y, bounds().w - gw - scrollbarWidth(), bounds().h}, bounds());
}

int TextView::visibleLineCount() const {
  if (!root()) return 1;
  return std::max(1, static_cast<int>(bounds().h / root()->text().lineHeight()));
}

Position TextView::positionAt(float x, float y) const {
  const Document* d = document();
  if (!d || !root()) return {0, 0};
  const Layout L = geometry();
  WrapLayout& W = wrapLayout();
  const int vrow = static_cast<int>(std::floor((y - bounds().y + std::floor(vp_.y())) / L.lineHeight()));
  const float lx = x - (bounds().x + L.textOffset(d->buffer().lineCount())) + scrollX();
  return W.positionAt(vrow, lx);
}

RectF TextView::caretRect(Position p) const {
  const Document* d = document();
  if (!d || !root()) return {0, 0, 0, 0};
  const Layout L = geometry();
  const TextBuffer& b = d->buffer();
  p = b.clamp(p);
  WrapLayout& W = wrapLayout();
  const float w = std::max(1.0f, std::floor(L.cellWidth() / 6));
  const int row = W.rowOfByte(p.line, p.column);
  return {bounds().x + L.textOffset(b.lineCount()) - scrollX() + W.xOf(p.line, row, p.column),
          bounds().y + L.lineTop(W.firstRowOf(p.line) + row, std::floor(vp_.y())), w, L.lineHeight()};
}

std::string TextView::positionText() const {
  const Document* d = document();
  if (!d || !isCurrent() || !root()) return {};
  const Position p = editor_.selections()[0].head;
  return "Ln " + std::to_string(p.line + 1) + ", Col " +
         std::to_string(geometry().columnForByte(d->buffer().line(p.line), static_cast<size_t>(p.column)) + 1);
}

float TextView::maxScrollY() const {
  const Document* d = document();
  if (!d || !root()) return 0;
  return std::max(0.0f, totalHeight() - bounds().h * 0.5f);
}

void TextView::onLayout() {
  syncWrap();
  const float sw = scrollbarWidth();
  sb_->layout({bounds().x + bounds().w - sw, bounds().y, sw, bounds().h});
  if (isCurrent()) editor_.setPageLines(visibleLineCount());
  if (pendingEnsure_) ensureCaretVisible(false);
}

void TextView::onFocusChanged(bool) {
  if (isCurrent()) editor_.setPageLines(visibleLineCount());
  caretOn_ = true;
  activityMs_ = now();
  markDirty();
}

void TextView::activate() {
  Document* d = document();
  if (!d) return;
  // A tab switch is not a navigation jump (§1.3): no history entry, the view's remembered caret comes back.
  if (d != editor_.currentDocument()) editor_.activateDocument(*d, lastCaret_);
  setFocus();
  editor_.setPageLines(visibleLineCount());
  syncState();
}

void TextView::ensureCaretVisible(bool animate) {
  if (!root() || !isCurrent() || bounds().h <= 0) {
    pendingEnsure_ = true;
    return;
  }
  pendingEnsure_ = false;
  const Layout L = geometry();
  const TextBuffer& b = document()->buffer();
  const Position p = b.clamp(editor_.selections()[0].head);
  const float lh = L.lineHeight(), cw = L.cellWidth(), h = bounds().h;
  const float margin = std::max(0.0f, std::min(2 * lh, std::floor((h - lh) / 2)));
  WrapLayout& W = wrapLayout();
  // The rows above the caret that can be on screen with it are examined first: its row index is only as good as the
  // line heights above it.
  for (int l = std::max(0, p.line - visibleLineCount()); W.enabled() && l < p.line; ++l) W.rowsEstimate(l);
  const float top = static_cast<float>(W.visualRowOf(p)) * lh;
  float ty = vp_.targetY();
  if (top - margin < ty) ty = top - margin;
  else if (top + lh + margin > ty + h) ty = top + lh + margin - h;
  ty = std::clamp(ty, 0.0f, maxScrollY());
  float tx = 0;
  if (!W.enabled()) {
    const float visW = bounds().w - scrollbarWidth() - L.textOffset(b.lineCount()),
                cx = L.xForByte(b.line(p.line), p.column);
    const float mx = std::min(3 * cw, visW / 4);
    tx = vp_.targetX();
    if (cx - mx < tx) tx = cx - mx;
    else if (cx + cw + mx > tx + visW) tx = cx + cw + mx - visW;
  }
  vp_.scrollTo(std::max(0.0f, tx), ty, animate && settings_.smoothScroll);
  markDirty();
}

void TextView::scrollBy(float dx, float dy) {
  const Document* d = document();
  if (!d || !root()) return;
  const Layout L = geometry();
  const TextBuffer& b = d->buffer();
  if (wordWrapActive()) {  // no horizontal scrolling while wrapping
    vp_.scrollTo(0, std::clamp(vp_.targetY() + dy, 0.0f, maxScrollY()), settings_.smoothScroll);
    markDirty();
    return;
  }
  int first, last;
  L.visibleLines(vp_.targetY(), bounds().h, b.lineCount(), first, last);
  float widest = 0;
  for (int i = first; i < last; ++i) widest = std::max(widest, L.lineWidth(b.line(i)));
  const float maxX =
      std::max(0.0f, widest + 2 * L.cellWidth() - (bounds().w - scrollbarWidth() - L.textOffset(b.lineCount())));
  vp_.scrollTo(std::clamp(vp_.targetX() + dx, 0.0f, std::max(maxX, vp_.targetX())),
               std::clamp(vp_.targetY() + dy, 0.0f, maxScrollY()), settings_.smoothScroll);
  markDirty();
}

// Compares the Editor with what was last seen; on a change keeps the caret visible and requests a repaint.
bool TextView::syncState() {
  const Document* d = document();
  const bool cur = isCurrent();
  const int ver = d ? d->version() : -1;
  const bool docChanged = d != seenDoc_, selChanged = cur && editor_.selections() != seenSel_;
  if (editor_.bookmarks().generation() != bookmarkGen_) {
    bookmarkGen_ = editor_.bookmarks().generation();
    markDirty();
  }
  const uint64_t sig = diagnosticSignature();
  if (sig != diagSig_ || docChanged) {  // clangd published new diagnostics (or another document)
    diagSig_ = sig;
    updateDiagMarkers();
    markDirty();
  }
  if (!docChanged && !selChanged && ver == seenVersion_) return false;
  seenDoc_ = d;
  seenVersion_ = ver;
  if (docChanged) bindDocument();
  if (cur) {
    seenSel_ = editor_.selections();
    lastCaret_ = seenSel_[0].head;
    std::vector<int> lines;
    for (const Selection& s : seenSel_) lines.push_back(s.head.line);
    setMarkLines(Scrollbar::Lane::Caret, std::move(lines));
  } else {
    seenSel_.clear();
    setMarkLines(Scrollbar::Lane::Caret, {});
  }
  if (docChanged) {
    vp_.jumpTo(0, 0);
    anchorLine_ = -1;
  }
  if (cur) ensureCaretVisible(!docChanged);
  caretOn_ = true;
  activityMs_ = now();
  markDirty();
  if (onStateChanged) onStateChanged();
  return true;
}

int TextView::onTick(int64_t nowMs) {
  syncState();
  int wait = -1;
  if (vp_.animating()) {
    const float dt = (lastTick_ < 0 || nowMs - lastTick_ > 100) ? 16.0f : static_cast<float>(nowMs - lastTick_);
    lastTick_ = nowMs;
    if (vp_.step(std::max(dt, 1.0f))) markDirty();
    if (vp_.animating()) wait = 16;
  } else {
    lastTick_ = -1;
  }
  if (hl_.pending()) {  // finishing the exact lexical pass behind a provisional first paint
    if (hl_.step(Highlighter::kSyncBudget)) markDirty();
    if (hl_.pending()) wait = wait < 0 ? 1 : std::min(wait, 1);
  }
  if (settings_.caretBlink && hasFocus() && isCurrent()) {
    const int64_t ms = std::max(50, settings_.caretBlinkMs), el = std::max<int64_t>(0, nowMs - activityMs_);
    const bool on = (el / ms) % 2 == 0;
    if (on != caretOn_) {
      caretOn_ = on;
      markDirty();
    }
    const int next = static_cast<int>(ms - el % ms);
    wait = wait < 0 ? next : std::min(wait, next);
  }
  return wait;
}

void TextView::drawRowText(PaintContext& ctx, const RowRef& r, float textX, float y, const RectF& clip,
                           const std::vector<gfx::ColorSpan>* spans) {
  const std::string& line = document()->buffer().line(r.line);
  const float x = textX + static_cast<float>(r.indent) * root()->text().cellWidth();
  const bool whole = r.start == 0 && r.end >= static_cast<int>(line.size());
  const std::string_view v =
      whole ? std::string_view(line)
            : std::string_view(line).substr(static_cast<size_t>(r.start), static_cast<size_t>(r.end - r.start));
  if (!spans) {
    ctx.text(x, y, v, ctx.theme.text, clip);
    return;
  }
  if (whole) {
    ctx.textRenderer.drawSpans(x, y, v, *spans, hl_.palette(), hl_.defaultColor(), clip);
    return;
  }
  // A row of a wrapped line: the spans that touch it, relative to the row start.
  rowSpans_.clear();
  const uint32_t a = static_cast<uint32_t>(r.start), e = static_cast<uint32_t>(r.end);
  auto it = std::partition_point(spans->begin(), spans->end(), [a](const gfx::ColorSpan& s) { return s.end <= a; });
  for (; it != spans->end() && it->begin < e; ++it)
    rowSpans_.push_back({std::max(it->begin, a) - a, std::min(it->end, e) - a, it->color});
  ctx.textRenderer.drawSpans(x, y, v, rowSpans_, hl_.palette(), hl_.defaultColor(), clip);
}

void TextView::paint(PaintContext& ctx) {
  const RectF& b = bounds();
  const Theme& th = ctx.theme;
  ctx.fill(b, Theme::orElse(th.editorBackground, th.panelBackground));
  const Document* d = document();
  if (!d) return;
  bindDocument();
  if (root()->themeGeneration() != themeGen_) {
    hl_.setTheme(th);
    themeGen_ = root()->themeGeneration();
  }
  syncWrap();
  const Layout L = geometry();
  const TextBuffer& buf = d->buffer();
  const int n = buf.lineCount();
  const float lh = L.lineHeight(), cw = L.cellWidth();
  WrapLayout& W = wrapLayout();
  // Looking at the rows on screen teaches the wrap map their real height. When that moves the line that was at the
  // top (it was scrolled to through rows that were only estimates) the view follows it, so the text does not jump.
  const int anchor = anchorLine_ >= 0 && anchorLine_ < n && W.enabled() ? anchorLine_ : -1;
  int anchorRow = anchor >= 0 ? W.firstRowOf(anchor) : 0;
  collectVisibleRows(vis_);
  for (int pass = 0; anchor >= 0 && pass < 3; ++pass) {
    const int now = W.firstRowOf(anchor);
    if (now == anchorRow) break;
    vp_.shiftY(static_cast<float>(now - anchorRow) * lh);
    anchorRow = now;
    collectVisibleRows(vis_);
  }
  const float sx = scrollX(), sy = std::floor(vp_.y());
  const float gw = L.gutterWidth(n), textX = b.x + L.textOffset(n) - sx;
  const RectF clip = textArea(), gutterClip = intersect({b.x, b.y, gw, b.h}, b);
  const int first = vis_.empty() ? 0 : vis_.front().line, last = vis_.empty() ? 0 : vis_.back().line + 1;
  if (!vis_.empty()) anchorLine_ = first;
  auto rowY = [&](const RowRef& r) { return b.y + L.lineTop(r.vrow, sy); };
  ctx.fill(gutterClip, th.gutterBackground);
  const bool colour = colouring();
  if (colour) hl_.prepare(first, last);
  if (W.generation() != markGen_) pushMarkers();
  sb_->setMetrics(maxScrollY(), b.h, sy, W.totalRows());
  if (settings_.columnGuide > 0) {
    const float gx = std::floor(textX + static_cast<float>(settings_.columnGuide) * cw);
    if (gx >= clip.x && gx < clip.x + clip.w) ctx.fill({gx, clip.y, 1, clip.h}, th.columnGuide);
  }

  if (!findMatches_.empty()) {  // find / replace matches of the visible lines, behind the text
    Color hit = th.dirtyMarker, now = th.dirtyMarker;
    hit.a = 0.30f;
    now.a = 0.65f;
    hit = Theme::orElse(th.findHighlight, hit);
    now = Theme::orElse(th.findHighlightCurrent, now);
    auto it = std::lower_bound(findMatches_.begin(), findMatches_.end(), first,
                               [](const Range& r, int line) { return r.end.line < line; });
    for (; it != findMatches_.end() && it->start.line < last; ++it) {
      if (it->start.line != it->end.line) continue;  // matches are clipped to their first line
      const bool current = static_cast<int>(it - findMatches_.begin()) == findCurrent_;
      forSegments(it->start.line, it->start.column, it->end.column, false, [&](const RowRef& r, float x0, float x1) {
        const RectF rc = intersect({textX + x0, rowY(r), std::max(x1 - x0, cw / 2), lh}, clip);
        if (rc.w > 0) ctx.fill(rc, current ? now : hit);
      });
    }
  }
  const bool cur = isCurrent();
  if (cur) {
    const std::vector<Selection>& sels = editor_.selections();
    const Position head = buf.clamp(sels[0].head);
    if (settings_.highlightCurrentLine && head.line >= first && head.line < last) {
      const int vrow = W.visualRowOf(head);
      if (!vis_.empty() && vrow >= vis_.front().vrow && vrow <= vis_.back().vrow) {
        const RectF line = intersect({clip.x, b.y + L.lineTop(vrow, sy), clip.w, lh}, clip);
        ctx.fill(line, Theme::orElse(th.currentLineFill, mix(th.panelBackground, th.text, 0.05f)));
        if (th.currentLineBorder.a > 0 && line.w > 2 && line.h > 2) {  // 1px outline (top, bottom, left, right)
          ctx.fill({line.x, line.y, line.w, 1}, th.currentLineBorder);
          ctx.fill({line.x, line.y + line.h - 1, line.w, 1}, th.currentLineBorder);
          ctx.fill({line.x, line.y, 1, line.h}, th.currentLineBorder);
          ctx.fill({line.x + line.w - 1, line.y, 1, line.h}, th.currentLineBorder);
        }
      }
    }
  }
  if (cur || th.selectionInactive.a > 0) {
    const std::vector<Selection>& sels = editor_.selections();
    Color selColor = th.tabAccent;
    selColor.a = 0.38f;
    selColor = cur ? Theme::orElse(th.selectionActive, selColor) : th.selectionInactive;
    for (const Selection& s : sels) {
      if (s.empty()) continue;
      const Range r = s.range();
      if (r.end.line < first || r.start.line >= last) continue;
      for (int l = std::max(first, r.start.line); l <= std::min(last - 1, r.end.line); ++l) {
        const int len = static_cast<int>(buf.line(l).size());
        forSegments(l, l == r.start.line ? r.start.column : 0, l == r.end.line ? r.end.column : len, l != r.end.line,
                    [&](const RowRef& row, float x0, float x1) {
                      const RectF rc = intersect({textX + x0, rowY(row), x1 - x0, lh}, clip);
                      if (rc.w > 0) ctx.fill(rc, selColor);
                    });
      }
    }
  }
  if (cur)
    if (const auto m = bracketMatch())
      for (const Position& p : {m->first, m->second})
        if (p.line >= first && p.line < last) {
          const auto [lo, hi] = visibleRange(p.line);
          const int len = static_cast<int>(buf.line(p.line).size());
          for (size_t i = lo; i < hi; ++i) {
            const RowRef& r = vis_[i];
            if (p.column < r.start || (p.column >= r.end && r.end < len)) continue;
            ctx.fill(intersect({textX + W.xOf(p.line, r.row, p.column), rowY(r), cw, lh}, clip), th.bracketMatch);
          }
        }
  bookmarkLines_ = editor_.bookmarks().lines(d->path());
  int spanLine = -1;
  const std::vector<gfx::ColorSpan>* spans = nullptr;
  for (const RowRef& r : vis_) {
    const float y = rowY(r);
    if (r.row == 0) {
      if (std::binary_search(bookmarkLines_.begin(), bookmarkLines_.end(), r.line)) {  // bookmark: a flag in the gutter
        const float m = std::max(4.0f, std::floor(cw * 0.7f));
        ctx.fill({b.x + 2, y + std::floor((lh - m) / 2), m, m}, th.tabAccent);
      }
      number_ = std::to_string(r.line + 1);
      ctx.text(b.x + gw - cw - static_cast<float>(number_.size()) * cw, y, number_, th.gutterText, gutterClip);
    }
    if (colour && r.line != spanLine) {
      spanLine = r.line;
      spans = &hl_.colorSpans(r.line);
    }
    drawRowText(ctx, r, textX, y, clip, colour ? spans : nullptr);
    if (settings_.showWhitespace) {
      const std::string& text = buf.line(r.line);
      paintWhitespace(ctx, L,
                      std::string_view(text).substr(static_cast<size_t>(r.start), static_cast<size_t>(r.end - r.start)),
                      textX + static_cast<float>(r.indent) * cw, y, clip);
    }
  }
}

// Dots for spaces and arrows for tabs, as rects / lines (no glyphs needed). Only the first 4000 bytes of a line.
void TextView::paintWhitespace(PaintContext& ctx, const Layout& L, std::string_view s, float textX, float y,
                               const RectF& clip) const {
  const Color c = ctx.theme.whitespace;
  const float cw = L.cellWidth(), lh = L.lineHeight(), dot = std::max(1.0f, std::floor(cw / 6));
  const int tw = root()->text().textLayout().tabWidth();
  const size_t end = std::min<size_t>(s.size(), 4000);
  int col = 0;
  for (size_t i = 0; i < end; ++i) {
    if (s[i] != ' ' && s[i] != '\t') continue;
    if (i == 0 || (s[i - 1] != ' ' && s[i - 1] != '\t')) col = L.columnForByte(s, i);
    const float x = textX + static_cast<float>(col) * cw;
    if (x > clip.x + clip.w) break;
    if (s[i] == ' ') {
      ++col;
      if (x + cw > clip.x) ctx.fill({std::floor(x + (cw - dot) / 2), std::floor(y + (lh - dot) / 2), dot, dot}, c);
      continue;
    }
    const int next = (col / tw + 1) * tw;
    const float x1 = textX + static_cast<float>(next) * cw, ym = std::floor(y + lh / 2), h = std::max(2.0f, lh / 6);
    col = next;
    if (x1 <= clip.x) continue;
    ctx.line(x + 2, ym, x1 - 2, ym, 1, c);
    ctx.line(x1 - 2, ym, x1 - 2 - h, ym - h, 1, c);
    ctx.line(x1 - 2, ym, x1 - 2 - h, ym + h, 1, c);
  }
}

std::optional<std::pair<Position, Position>> TextView::bracketMatch() {
  const Document* d = document();
  if (!d || !isCurrent() || !root() || !colouring()) return std::nullopt;
  const TextBuffer& b = d->buffer();
  const Position p = b.clamp(editor_.selections()[0].head);
  const std::string& text = b.line(p.line);
  static const char kBr[] = "()[]{}";
  for (int back = 0; back < 2; ++back) {  // the character after the caret first, then the one before it
    const int col = p.column - back;
    if (col < 0 || col >= static_cast<int>(text.size()) || !text[static_cast<size_t>(col)]) continue;
    const char* at = std::strchr(kBr, text[static_cast<size_t>(col)]);
    const std::vector<LexSpan>* here = hl_.lineSpans(p.line);
    if (!at || !here) continue;
    bool punct = false;  // brackets inside comments / strings are not brackets
    for (const LexSpan& s : *here) punct |= s.kind == Lex::Punct && s.begin == static_cast<uint32_t>(col);
    if (!punct) continue;
    // Search only where the exact lexical pass is (or can cheaply be) ahead of us; a cold far-away caret waits for it.
    if (p.line + kBracketLines > hl_.exactLines() + Highlighter::kSyncBudget) return std::nullopt;
    hl_.ensureExact(std::min(b.lineCount() - 1, p.line + kBracketLines));
    const bool open = (at - kBr) % 2 == 0;
    const char self = *at, other = open ? at[1] : at[-1];
    int depth = 1;
    for (int l = p.line; l >= 0 && l < b.lineCount() && std::abs(l - p.line) <= kBracketLines; l += open ? 1 : -1) {
      const std::vector<LexSpan>* spans = hl_.exactSpans(l);
      if (!spans) break;
      const std::string& ln = b.line(l);
      for (size_t k = 0; k < spans->size(); ++k) {
        const LexSpan& s = (*spans)[open ? k : spans->size() - 1 - k];
        if (s.kind != Lex::Punct ||
            (l == p.line && (open ? static_cast<int>(s.begin) <= col : static_cast<int>(s.begin) >= col)))
          continue;
        if (ln[s.begin] == self) ++depth;
        else if (ln[s.begin] == other && --depth == 0)
          return std::make_pair(Position{p.line, col}, Position{l, static_cast<int>(s.begin)});
      }
    }
  }
  return std::nullopt;
}

// Wavy underline under a diagnostic range: short line segments zig-zagging 2 px at the bottom of the line box.
void TextView::paintSquiggles(PaintContext& ctx, const Layout& L, float sx, float sy) {
  const Document* d = document();
  const TextBuffer& buf = d->buffer();
  const RectF clip = textArea();
  if (vis_.empty()) return;
  const int first = vis_.front().line, last = vis_.back().line + 1;
  const float x0 = bounds().x + L.textOffset(buf.lineCount()) - sx, cw = L.cellWidth(),
              amp = std::max(1.0f, std::round(L.lineHeight() / 8));
  const float step = std::max(2.0f, std::round(cw / 2));
  // Build diagnostics (C1) first so that clangd's squiggle wins where both mark the same text; notes are not drawn.
  for (const bool build : {true, false})
    for (const Diagnostic& g : build ? editor_.buildDiagnostics(d->path()) : editor_.diagnostics(d->path())) {
      const Range r = g.range;
      if (r.end.line < first || r.start.line >= last || (build && g.severity > 2)) continue;
      const Color col = build             ? (g.severity <= 1 ? ctx.theme.buildError : ctx.theme.buildWarning)
                        : g.severity <= 1 ? ctx.theme.diagnosticError
                        : g.severity == 2 ? ctx.theme.diagnosticWarning
                                          : ctx.theme.diagnosticInfo;
      for (int l = std::max(first, r.start.line); l <= std::min(last - 1, r.end.line); ++l) {
        const int len = static_cast<int>(buf.line(l).size());
        forSegments(l, l == r.start.line ? r.start.column : 0, l == r.end.line ? r.end.column : len, false,
                    [&](const RowRef& row, float a, float e) {
                      if (e < a + cw) e = a + cw;  // empty range: one cell
                      const float yb = bounds().y + L.lineTop(row.vrow, sy) + L.lineHeight() - amp - 1;
                      bool up = false;
                      for (float x = a; x < e; x += step, up = !up) {
                        const float xe = std::min(x + step, e);
                        if (x0 + xe < clip.x || x0 + x > clip.x + clip.w) continue;
                        ctx.line(x0 + x, up ? yb : yb + amp, x0 + xe, up ? yb + amp : yb, 1, col);
                      }
                    });
      }
    }
}

// Carets go in the overlay layer: all text is emitted after all rects, so they would otherwise be hidden by it.
void TextView::paintOverlay(PaintContext& ctx) {
  if (!document() || !root()) return;
  collectVisibleRows(vis_);
  const Layout L = geometry();
  const TextBuffer& buf = document()->buffer();
  const float sy = std::floor(vp_.y()), sx = scrollX();
  paintSquiggles(ctx, L, sx, sy);
  if (!isCurrent() || !(caretOn_ || !settings_.caretBlink || !hasFocus()) || vis_.empty()) return;
  const int first = vis_.front().line, last = vis_.back().line + 1;
  const RectF clip = textArea();
  const float w = std::max(1.0f, std::floor(L.cellWidth() / 6)), x0 = bounds().x + L.textOffset(buf.lineCount()) - sx;
  const Color c = hasFocus() ? Theme::orElse(ctx.theme.editorCaret, ctx.theme.text) : ctx.theme.textDim;
  for (const Selection& s : editor_.selections()) {
    const Position p = s.head;
    if (p.line < first || p.line >= last) continue;
    const auto [lo, hi] = visibleRange(p.line);
    const int len = static_cast<int>(buf.line(p.line).size());
    for (size_t i = lo; i < hi; ++i) {
      const RowRef& r = vis_[i];
      if (p.column < r.start || (p.column >= r.end && r.end < len)) continue;
      const RectF rc = intersect(
          {x0 + wrap_.xOf(p.line, r.row, p.column), bounds().y + L.lineTop(r.vrow, sy), w, L.lineHeight()}, clip);
      if (rc.w > 0) ctx.fill(rc, c);
      break;
    }
  }
}

// Caret keys by visual row (wrap on): Up / Down / PageUp / PageDown keep the x of the row they started from for as
// long as only vertical moves follow; Home / End go to the row's start / end first and to the line's on a second
// press. Same selection rules as Editor::moveCarets: `extend` keeps the anchors, the top of the document and the
// bottom of the last line are the targets of Up / Down at the ends.
bool TextView::moveCaretsVisual(Editor::Direction dir, Editor::Unit unit, bool extend) {
  using D = Editor::Direction;
  const bool vertical = dir == D::Up || dir == D::Down || dir == D::PageUp || dir == D::PageDown;
  if (!(vertical || dir == D::LineStart || dir == D::LineEnd) || !isCurrent() || !root() || !wordWrapActive())
    return false;
  const TextBuffer& b = document()->buffer();
  WrapLayout& W = wrapLayout();
  const float cw = root()->text().cellWidth();
  std::vector<Selection> sels = editor_.selections();
  const bool keep = vertical && prefSel_ == sels && prefCol_.size() == sels.size();
  std::vector<int> cols(sels.size(), 0);
  const bool up = dir == D::Up || dir == D::PageUp;
  const int steps = (dir == D::PageUp || dir == D::PageDown || unit == Editor::Unit::Page) ? visibleLineCount() : 1;
  for (size_t i = 0; i < sels.size(); ++i) {
    Position h = b.clamp(sels[i].head);
    const int row = W.rowOfByte(h.line, h.column);
    if (vertical) {
      cols[i] = keep ? prefCol_[i] : static_cast<int>(std::lround(W.xOf(h.line, row, h.column) / cw));
      int line = h.line, r = row, moved = 0;
      while (moved < steps && (up ? W.prevRow(line, r) : W.nextRow(line, r))) ++moved;
      if (moved > 0) h = W.positionIn(line, r, static_cast<float>(cols[i]) * cw);
      else if (up) h = {0, 0};  // already on the first / last row: the very start / end, as in Editor::moveCarets
      else h = {line, static_cast<int>(b.line(line).size())};
    } else if (dir == D::LineStart) {
      const int start = W.rowStart(h.line, row);
      h.column = h.column != start ? start : 0;
    } else {
      const int edge = W.rowLastCaret(h.line, row);
      h.column = h.column != edge ? edge : static_cast<int>(b.line(h.line).size());
    }
    sels[i].head = h;
    if (!extend) sels[i].anchor = h;
  }
  editor_.setSelections(sels);
  prefSel_.clear();
  if (vertical && editor_.selections() == sels) {
    prefSel_ = sels;
    prefCol_ = std::move(cols);
  }
  return true;
}

bool TextView::onEvent(const InputEvent& e) {
  if (!document() || !root()) return false;
  bool handled = false;
  switch (e.type) {
    case InputType::Wheel: {
      const Layout L = geometry();
      const float notch = static_cast<float>(settings_.wheelLines);
      if (e.mods & ModShift) scrollBy(-e.dy * notch * L.cellWidth(), 0);
      else scrollBy(-e.dx * notch * L.cellWidth(), -e.dy * notch * L.lineHeight());
      return true;
    }
    case InputType::MouseDown:
      handled = e.button == MouseButton::Left;
      if (handled) mouseDown(e);
      break;
    case InputType::MouseMove:
      handled = hasCapture() && drag_ != Drag::None;
      if (handled) mouseDrag(e);
      break;
    case InputType::MouseUp:
      handled = e.button == MouseButton::Left;
      drag_ = Drag::None;
      break;
    case InputType::Text:
      if (!isCurrent()) activate();
      if (!e.text.empty()) editor_.typeText(e.text);
      handled = !e.text.empty();
      break;
    default: break;
  }
  if (handled) {
    caretOn_ = true;
    activityMs_ = now();
    markDirty();
    syncState();
  }
  return handled;
}

void TextView::mouseDown(const InputEvent& e) {
  if (!isCurrent()) activate();
  const TextBuffer& b = document()->buffer();
  const Position pos = b.clamp(positionAt(e.x, e.y));
  const bool shift = e.mods & ModShift, ctrl = e.mods & ModCtrl, alt = e.mods & ModAlt;
  drag_ = Drag::Char;
  anchor_ = anchorEnd_ = lastDragPos_ = pos;
  base_.clear();
  if (e.clicks >= 3) {  // whole line including its break (the last line: up to its end)
    anchor_ = {pos.line, 0};
    anchorEnd_ = pos.line + 1 < b.lineCount() ? Position{pos.line + 1, 0}
                                              : Position{pos.line, static_cast<int>(b.line(pos.line).size())};
    editor_.setSelections({{anchor_, anchorEnd_}});
  } else if (e.clicks == 2) {
    const Range w = b.wordAt(pos).range;
    anchor_ = w.start;
    anchorEnd_ = w.end;
    editor_.setSelections({{anchor_, anchorEnd_}});
  } else if (shift) {
    anchor_ = anchorEnd_ = editor_.selections()[0].anchor;
    editor_.setSelections({{anchor_, pos}});
  } else if (ctrl || alt) {  // add a caret; dragging then grows it (Ctrl) or turns into a column selection (Alt)
    base_ = editor_.selections();
    drag_ = alt ? Drag::Column : Drag::AddCaret;
    std::vector<Selection> v{caretAt(pos)};
    v.insert(v.end(), base_.begin(), base_.end());
    editor_.setSelections(std::move(v));
  } else {
    editor_.setSelections({caretAt(pos)});
  }
  captureMouse();
}

void TextView::mouseDrag(const InputEvent& e) {
  const Position pos = document()->buffer().clamp(positionAt(e.x, e.y));
  if (pos == lastDragPos_) return;
  lastDragPos_ = pos;
  if (drag_ == Drag::Column) {
    editor_.columnSelect(anchor_, pos);
  } else if (drag_ == Drag::AddCaret) {
    std::vector<Selection> v{{anchor_, pos}};
    v.insert(v.end(), base_.begin(), base_.end());
    editor_.setSelections(std::move(v));
  } else if (pos < anchor_) {  // grow from the clicked word / line (or point) in either direction
    editor_.setSelections({{anchorEnd_, pos}});
  } else if (anchorEnd_ < pos) {
    editor_.setSelections({{anchor_, pos}});
  } else {
    editor_.setSelections({{anchor_, anchorEnd_}});
  }
}

}  // namespace tf::ui
