#include "ui/features/BuildPanel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tf::ui {

namespace {
constexpr size_t kMaxLines = 20000, kMaxLineBytes = 4096;
constexpr int kWheelRows = 3;

std::string count(size_t n, const char* noun) { return std::to_string(n) + " " + noun + (n == 1 ? "" : "s"); }

const char* severityName(BuildSeverity s) {
  return s == BuildSeverity::Error ? "error" : s == BuildSeverity::Warning ? "warning" : "note";
}
}  // namespace

BuildPanel::BuildPanel(Editor& editor) : editor_(editor) {
  auto sb = std::make_unique<Scrollbar>();
  sb_ = sb.get();
  sb_->onScroll = [this](float y, bool) { scrollTo(y, true); };
  addChild(std::move(sb));
}

BuildPanel::~BuildPanel() = default;

// ---------------------------------------------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------------------------------------------

std::string BuildPanel::clean(std::string_view raw) {
  std::string out;
  out.reserve(raw.size());
  for (size_t i = 0; i < raw.size();) {
    const unsigned char c = static_cast<unsigned char>(raw[i]);
    if (c == 0x1b) {  // ESC: CSI (colours, cursor), OSC (titles, links) or a two-character sequence
      ++i;
      if (i < raw.size() && raw[i] == '[') {
        while (++i < raw.size() && (raw[i] < 0x40 || raw[i] > 0x7e)) {}
        ++i;
      } else if (i < raw.size() && raw[i] == ']') {
        while (++i < raw.size() && raw[i] != 0x07 && !(raw[i] == 0x1b && i + 1 < raw.size() && raw[i + 1] == '\\')) {}
        i += i < raw.size() && raw[i] == 0x1b ? 2 : 1;
      } else {
        ++i;
      }
      continue;
    }
    ++i;
    if (c == '\r') {
      if (i < raw.size()) out.clear();  // progress output: what follows overwrites the line
    } else if (c == '\t') {
      out += "    ";
    } else if (c >= 0x20 && c != 0x7f) {
      out += static_cast<char>(c);
    }
  }
  if (out.size() > kMaxLineBytes) {
    size_t cut = kMaxLineBytes;
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;  // keep UTF-8 whole
    out.resize(cut);
    out += "\xE2\x80\xA6";
  }
  return out;
}

std::string BuildPanel::statusText() const {
  switch (state_) {
    case State::Idle: return "No build yet";
    case State::Running: return "Building\xE2\x80\xA6";
    case State::Cancelled: return "Cancelled";
    case State::Succeeded: {
      char buf[48];
      std::snprintf(buf, sizeof buf, "%.1f", static_cast<double>(result_.elapsedMs) / 1000.0);
      return std::string("Succeeded in ") + buf + " s" +
             (result_.warnings ? ", " + count(result_.warnings, "warning") : std::string());
    }
    case State::Failed:
      if (result_.startFailed || !startError_.empty())
        return "Failed to start: " + (startError_.empty() ? result_.error : startError_);
      if (result_.errors == 0 && result_.warnings == 0)
        return "Failed (exit code " + std::to_string(result_.exitCode) + ")";
      return "Failed: " + count(result_.errors, "error") + ", " + count(result_.warnings, "warning");
  }
  return {};
}

void BuildPanel::changed() {
  markDirty();
  if (onChanged) onChanged();
}

// ---------------------------------------------------------------------------------------------------------------
// Running a build
// ---------------------------------------------------------------------------------------------------------------

bool BuildPanel::start(const BuildOverrides& overrides, std::string* error) {
  std::string err;
  BuildCommand cmd;
  if (!detectBuildCommand(editor_.workspace(), overrides, &cmd, &err)) {
    ++gen_;
    startError_ = err;
    state_ = State::Failed;
    if (error) *error = err;
    changed();
    return false;
  }
  const int gen = ++gen_;
  std::weak_ptr<int> life = life_;
  const bool ok = editor_.build(
      overrides,
      [this, life, gen](const std::vector<std::string>& ls) {
        if (!life.expired() && gen == gen_) appendLines(ls);
      },
      [this, life, gen](const BuildResult& r) {
        if (!life.expired() && gen == gen_) finish(r);
      },
      &err);
  if (!ok) {
    startError_ = err;
    state_ = State::Failed;
    if (error) *error = err;
    changed();
    return false;
  }
  startError_.clear();
  lines_.clear();
  messages_.clear();
  errMarks_.clear();
  warnMarks_.clear();
  truncated_ = false;
  selected_ = hover_ = -1;
  scrollY_ = 0;
  follow_ = true;
  result_ = {};
  parser_ = std::make_unique<ErrorParser>(cmd.cwd.generic_string());
  lines_.push_back({"> " + clean(cmd.display), -1});
  state_ = State::Running;
  sb_->setMarkers(Scrollbar::Lane::Error, {});
  sb_->setMarkers(Scrollbar::Lane::Warning, {});
  updateMetrics();
  changed();
  return true;
}

void BuildPanel::cancel() {
  if (editor_.buildRunning()) editor_.cancelBuild();
}

void BuildPanel::appendLines(const std::vector<std::string>& raw) {
  for (const std::string& r : raw) {
    if (lines_.size() >= kMaxLines) {
      if (!truncated_) lines_.push_back({"[output truncated]", -1});
      truncated_ = true;
      break;
    }
    Line l{clean(r), -1};
    BuildMessage m;
    if (parser_ && parser_->parseLine(l.text, &m)) {
      l.message = static_cast<int>(messages_.size());
      if (m.severity == BuildSeverity::Error) errMarks_.push_back(static_cast<int>(lines_.size()));
      else if (m.severity == BuildSeverity::Warning) warnMarks_.push_back(static_cast<int>(lines_.size()));
      messages_.push_back(std::move(m));
    }
    lines_.push_back(std::move(l));
  }
  sb_->setMarkers(Scrollbar::Lane::Error, errMarks_);
  sb_->setMarkers(Scrollbar::Lane::Warning, warnMarks_);
  updateMetrics();
  changed();
}

void BuildPanel::finish(const BuildResult& r) {
  result_ = r;
  state_ = r.cancelled ? State::Cancelled : r.ok() ? State::Succeeded : State::Failed;
  if (r.startFailed) startError_ = r.error;
  changed();
}

// ---------------------------------------------------------------------------------------------------------------
// Selection and jumping
// ---------------------------------------------------------------------------------------------------------------

bool BuildPanel::clickable(int i) const {
  return i >= 0 && i < static_cast<int>(lines_.size()) && lines_[static_cast<size_t>(i)].message >= 0 &&
         messages_[static_cast<size_t>(lines_[static_cast<size_t>(i)].message)].hasLocation();
}

bool BuildPanel::activate(int i) {
  if (!clickable(i)) return false;
  const BuildMessage& m = messages_[static_cast<size_t>(lines_[static_cast<size_t>(i)].message)];
  std::string err;
  const Location loc{std::filesystem::path(m.file), {std::max(m.line - 1, 0), std::max(m.col - 1, 0)}};
  if (!editor_.gotoLocation(loc, &err, std::string(severityName(m.severity)) + ": " + m.text)) {
    if (onMessage) onMessage(err.empty() ? "Cannot open " + m.file : err);
    return false;
  }
  return true;
}

bool BuildPanel::selectMessage(const BuildMessage& m) {
  for (size_t i = 0; i < lines_.size(); ++i) {
    if (lines_[i].message < 0) continue;
    const BuildMessage& c = messages_[static_cast<size_t>(lines_[i].message)];
    if (c.file == m.file && c.line == m.line && c.col == m.col && c.severity == m.severity) {
      select(static_cast<int>(i));
      return true;
    }
  }
  return false;
}

void BuildPanel::select(int i) {
  selected_ = i;
  if (i >= 0) ensureVisible(i);
  markDirty();
}

void BuildPanel::ensureVisible(int i) {
  const float rh = rowHeight(), top = static_cast<float>(i) * rh, view = bodyRect().h;
  if (top < scrollY_) scrollTo(top, true);
  else if (top + rh > scrollY_ + view) scrollTo(top + rh - view, true);
}

// ---------------------------------------------------------------------------------------------------------------
// Geometry and scrolling
// ---------------------------------------------------------------------------------------------------------------

float BuildPanel::rowHeight() const { return root() ? root()->text().lineHeight() : 14.0f; }
float BuildPanel::headerHeight() const { return rowHeight() + 2 * theme().verticalPad; }
float BuildPanel::scrollbarWidth() const { return root() ? std::round(root()->text().cellWidth() * 1.5f) : 0.0f; }

RectF BuildPanel::headerRect() const { return {bounds().x, bounds().y, bounds().w, headerHeight()}; }

RectF BuildPanel::bodyRect() const {
  const float hh = headerHeight();
  return {bounds().x, bounds().y + hh, bounds().w, std::max(0.0f, bounds().h - hh)};
}

RectF BuildPanel::lineRect(int i) const {
  const RectF b = bodyRect();
  const float rh = rowHeight();
  return {b.x, b.y + static_cast<float>(i) * rh - scrollY_, b.w - scrollbarWidth(), rh};
}

float BuildPanel::maxScroll() const {
  return std::max(0.0f, static_cast<float>(lines_.size()) * rowHeight() - bodyRect().h);
}

void BuildPanel::updateMetrics() {
  const float range = maxScroll();
  if (follow_) scrollY_ = range;
  scrollY_ = std::clamp(scrollY_, 0.0f, range);
  sb_->setMetrics(range, bodyRect().h, scrollY_, static_cast<int>(lines_.size()));
  markDirty();
}

// A user scroll (wheel, scrollbar, keyboard selection): following resumes only at the very end.
void BuildPanel::scrollTo(float y, bool /*user*/) {
  const float range = maxScroll();
  scrollY_ = std::clamp(y, 0.0f, range);
  follow_ = scrollY_ >= range - 1.0f;
  sb_->setMetrics(range, bodyRect().h, scrollY_, static_cast<int>(lines_.size()));
  markDirty();
}

int BuildPanel::lineAt(float y) const {
  const RectF b = bodyRect();
  if (y < b.y || y >= b.y + b.h) return -1;
  const int i = static_cast<int>(std::floor((y - b.y + scrollY_) / rowHeight()));
  return i >= 0 && i < static_cast<int>(lines_.size()) ? i : -1;
}

void BuildPanel::layoutButtons(std::vector<Button>& out) const {
  const Theme& th = theme();
  const RectF h = headerRect();
  const bool running = state_ == State::Running;
  out = {{"Build", {}, true}, {"Cancel", {}, running}, {"\xC3\x97", {}, true}};
  float x = h.x + h.w - th.padding / 2;
  for (size_t k = out.size(); k-- > 0;) {
    const float w = root() ? root()->text().measure(out[k].label) + 2 * th.padding : 40.0f;
    x -= w;
    out[k].rect = {x, h.y + 2, w, h.h - 4};
    x -= th.padding / 2;
  }
}

RectF BuildPanel::buildButtonRect() const {
  std::vector<Button> b;
  layoutButtons(b);
  return b[0].rect;
}
RectF BuildPanel::cancelButtonRect() const {
  std::vector<Button> b;
  layoutButtons(b);
  return b[1].rect;
}
RectF BuildPanel::closeButtonRect() const {
  std::vector<Button> b;
  layoutButtons(b);
  return b[2].rect;
}

void BuildPanel::onLayout() {
  const RectF b = bodyRect();
  const float sw = scrollbarWidth();
  sb_->layout({b.x + b.w - sw, b.y, sw, b.h});
  updateMetrics();
}

// ---------------------------------------------------------------------------------------------------------------
// Painting and input
// ---------------------------------------------------------------------------------------------------------------

void BuildPanel::paint(PaintContext& ctx) {
  const Theme& th = ctx.theme;
  ctx.fill(bounds(), th.panelBackground);
  const RectF h = headerRect();
  ctx.fill(h, th.tabStripBackground);
  std::vector<Button> buttons;
  layoutButtons(buttons);
  const float textTop = h.y + th.verticalPad;
  const Color status = state_ == State::Failed ? th.buildError : state_ == State::Running ? th.text : th.textDim;
  ctx.text(h.x + th.padding, textTop, statusText(), status,
           {h.x, h.y, std::max(0.0f, buttons[0].rect.x - h.x - th.padding), h.h});
  for (size_t k = 0; k < buttons.size(); ++k) {
    const Button& b = buttons[k];
    ctx.fill(b.rect, static_cast<int>(k) == hotButton_ && b.enabled ? th.tabHover : th.tabInactive);
    ctx.text(b.rect.x + th.padding, textTop, b.label, b.enabled ? th.text : th.textDim, b.rect);
  }

  const RectF body = bodyRect();
  const RectF clip{body.x, body.y, body.w - scrollbarWidth(), body.h};
  const float rh = rowHeight();
  if (lines_.empty()) {
    ctx.text(clip.x + th.padding, clip.y + 2, "Build output appears here", th.textDim, clip);
    return;
  }
  const int first = std::max(0, static_cast<int>(std::floor(scrollY_ / rh)));
  const int last = std::min(static_cast<int>(lines_.size()), static_cast<int>(std::ceil((scrollY_ + body.h) / rh)));
  ctx.renderer.pushScissor(clip);
  for (int i = first; i < last; ++i) {
    if (i == selected_) ctx.fill(lineRect(i), th.selection);
    else if (i == hover_) ctx.fill(lineRect(i), th.tabHover);
  }
  ctx.renderer.popScissor();
  for (int i = first; i < last; ++i) {
    const Line& l = lines_[static_cast<size_t>(i)];
    Color c = th.text;
    if (l.message >= 0) {
      const BuildSeverity s = messages_[static_cast<size_t>(l.message)].severity;
      c = s == BuildSeverity::Error ? th.buildError : s == BuildSeverity::Warning ? th.buildWarning : th.textDim;
    } else if (i == 0) {
      c = th.textDim;  // the command line
    }
    ctx.text(clip.x + th.padding, lineRect(i).y, l.text, c, clip);
  }
}

void BuildPanel::onHover(bool entered) {
  if (entered) return;
  if (hover_ != -1 || hotButton_ != -1) markDirty();
  hover_ = hotButton_ = -1;
}

bool BuildPanel::onEvent(const InputEvent& e) {
  switch (e.type) {
    case InputType::MouseMove: {
      int hb = -1;
      std::vector<Button> bs;
      layoutButtons(bs);
      for (size_t k = 0; k < bs.size(); ++k)
        if (ui::contains(bs[k].rect, e.x, e.y)) hb = static_cast<int>(k);
      int hl = lineAt(e.y);
      if (!clickable(hl) || e.x >= bodyRect().x + bodyRect().w - scrollbarWidth()) hl = -1;
      if (hb != hotButton_ || hl != hover_) markDirty();
      hotButton_ = hb;
      hover_ = hl;
      return false;
    }
    case InputType::MouseDown: {
      if (e.button != MouseButton::Left) return false;
      if (ui::contains(headerRect(), e.x, e.y)) {
        std::vector<Button> bs;
        layoutButtons(bs);
        for (size_t k = 0; k < bs.size(); ++k) {
          if (!ui::contains(bs[k].rect, e.x, e.y) || !bs[k].enabled) continue;
          if (k == 0) {
            std::string err;
            if (!start({}, &err) && onMessage) onMessage("Build failed to start: " + err);
          } else if (k == 1) {
            cancel();
          } else if (onClose) {
            onClose();
          }
          return true;
        }
        return true;
      }
      const int i = lineAt(e.y);
      if (i < 0) return true;
      select(i);
      if (activate(i) && onJumped) onJumped();
      return true;
    }
    case InputType::Wheel:
      if (e.dy == 0) return false;
      scrollTo(scrollY_ - e.dy * kWheelRows * rowHeight(), true);
      return true;
    case InputType::KeyDown: {
      const int n = static_cast<int>(lines_.size());
      const int page = std::max(1, static_cast<int>(bodyRect().h / rowHeight()) - 1);
      switch (e.key) {
        case Key::Up: select(std::max(0, (selected_ < 0 ? n : selected_) - 1)); return n > 0;
        case Key::Down: select(std::min(n - 1, selected_ + 1)); return n > 0;
        case Key::PageUp: select(std::max(0, (selected_ < 0 ? n : selected_) - page)); return n > 0;
        case Key::PageDown: select(std::min(n - 1, selected_ + page)); return n > 0;
        case Key::Home: select(0); return n > 0;
        case Key::End: select(n - 1); return n > 0;
        case Key::Enter:
        case Key::KpEnter:
          if (activate(selected_) && onJumped) onJumped();
          return true;
        case Key::Escape:
          if (!onClose) return false;
          onClose();
          return true;
        default: return false;
      }
    }
    default: return false;
  }
}

}  // namespace tf::ui
