#include "ui/settings/Settings.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "ui/settings/Theme.h"
#include "ui/widgets/Widget.h"
#include "workspace/FileWatcher.h"

namespace tf::ui {

namespace {

const char kDefaults[] =
#include "DefaultSettings.inc"
    ;

std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

bool readFile(const std::filesystem::path& p, std::string& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

}  // namespace

const std::vector<SettingInfo>& settingsSchema() {
  static const std::vector<SettingInfo> s = {{"editor.smoothScroll", SettingType::Bool},
                                             {"editor.wordWrap", SettingType::Bool},
                                             {"editor.caretBlink", SettingType::Bool},
                                             {"editor.caretBlinkMs", SettingType::Int},
                                             {"editor.highlightCurrentLine", SettingType::Bool},
                                             {"editor.wheelLines", SettingType::Int},
                                             {"editor.tabWidth", SettingType::Int},
                                             {"editor.columnGuide", SettingType::Int},
                                             {"editor.showWhitespace", SettingType::Bool},
                                             {"font.editor", SettingType::Font},
                                             {"font.fallbacks", SettingType::String},
                                             {"font.size", SettingType::Float},
                                             {"ui.theme", SettingType::String},
                                             {"nav.statusCounts", SettingType::Bool},
                                             {"picker.preview", SettingType::Bool}};
  return s;
}

bool SettingValue::operator==(const SettingValue& o) const {
  return type == o.type && b == o.b && i == o.i && f == o.f && s == o.s && font == o.font && c.r == o.c.r &&
         c.g == o.c.g && c.b == o.c.b && c.a == o.c.a;
}

bool parseColor(std::string_view t, gfx::Color& out, std::string& error) {
  t = trim(t);
  if ((t.size() != 7 && t.size() != 9) || t[0] != '#') {
    error = "expected colour #rrggbb or #rrggbbaa, got '" + std::string(t) + "'";
    return false;
  }
  float v[4] = {0, 0, 0, 255};
  for (size_t k = 0; 1 + 2 * k < t.size(); ++k) {
    int n = 0;
    for (size_t j = 0; j < 2; ++j) {
      char ch = t[1 + 2 * k + j];
      if (!std::isxdigit(static_cast<unsigned char>(ch))) {
        error = "bad hex digit in colour '" + std::string(t) + "'";
        return false;
      }
      n = n * 16 + (std::isdigit(static_cast<unsigned char>(ch)) ? ch - '0' : std::tolower(ch) - 'a' + 10);
    }
    v[k] = static_cast<float>(n);
  }
  out = {v[0] / 255.f, v[1] / 255.f, v[2] / 255.f, v[3] / 255.f};
  return true;
}

bool parseSettingValue(SettingType type, std::string_view text, SettingValue& out, std::string& error) {
  text = trim(text);
  out = SettingValue();
  out.type = type;
  switch (type) {
    case SettingType::Bool:
      if (text == "true") out.b = true;
      else if (text == "false") out.b = false;
      else {
        error = "expected true or false, got '" + std::string(text) + "'";
        return false;
      }
      return true;
    case SettingType::Int: {
      std::string s(text);
      char* end = nullptr;
      errno = 0;
      long long v = std::strtoll(s.c_str(), &end, 10);
      if (s.empty() || *end || errno) {
        error = "expected an integer, got '" + s + "'";
        return false;
      }
      out.i = v;
      out.f = static_cast<double>(v);
      return true;
    }
    case SettingType::Float: {
      std::string s(text);
      char* end = nullptr;
      errno = 0;
      double v = std::strtod(s.c_str(), &end);
      if (s.empty() || *end || errno) {
        error = "expected a number, got '" + s + "'";
        return false;
      }
      out.f = v;
      out.i = static_cast<long long>(v);
      return true;
    }
    case SettingType::String:
      if (text.size() >= 2 && text.front() == '"' && text.back() == '"') text = text.substr(1, text.size() - 2);
      out.s = std::string(text);
      return true;
    case SettingType::Color: return parseColor(text, out.c, error);
    case SettingType::Font: {
      if (text.size() >= 2 && text.front() == '"' && text.back() == '"') text = text.substr(1, text.size() - 2);
      size_t comma = text.rfind(',');
      if (comma != std::string_view::npos) {
        std::string sz(trim(text.substr(comma + 1)));
        char* end = nullptr;
        double v = std::strtod(sz.c_str(), &end);
        if (sz.empty() || *end || v <= 0) {
          error = "expected a font size in pt after ',', got '" + sz + "'";
          return false;
        }
        out.font.sizePt = static_cast<float>(v);
        text = text.substr(0, comma);
      }
      out.font.family = std::string(trim(text));
      out.s = out.font.family;
      return true;
    }
  }
  return false;
}

void parseKeyValueLines(std::string_view text, std::string_view source, std::vector<SettingsDiag>& diags,
                        const std::function<void(int line, std::string_view key, std::string_view value)>& fn) {
  auto diag = [&](int line, bool warning, std::string msg) {
    diags.push_back({std::string(source), line, warning, std::move(msg)});
  };
  int line = 0;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    std::string_view raw = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
    pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
    ++line;
    std::string_view l = trim(raw);
    if (l.empty() || l[0] == '#') continue;
    if (l[0] == '[') {
      diag(line, false, "sections are not supported; use dotted keys (editor.tabWidth = 4)");
      continue;
    }
    size_t eq = l.find('=');
    if (eq == std::string_view::npos) {
      diag(line, false, "expected 'key = value'");
      continue;
    }
    std::string_view key = trim(l.substr(0, eq));
    bool ok = !key.empty();
    for (char ch : key) ok = ok && (std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '_');
    if (!ok) {
      diag(line, false, key.empty() ? "missing key before '='" : "invalid key '" + std::string(key) + "'");
      continue;
    }
    fn(line, key, trim(l.substr(eq + 1)));
  }
}

void parseSettings(std::string_view text, std::string_view source, std::map<std::string, SettingValue>& into,
                   std::vector<SettingsDiag>& diags) {
  std::set<std::string> seen;
  parseKeyValueLines(text, source, diags, [&](int line, std::string_view key, std::string_view value) {
    const SettingInfo* info = nullptr;
    for (auto& i : settingsSchema())
      if (key == i.key) info = &i;
    if (!info) {
      diags.push_back({std::string(source), line, false, "unknown setting '" + std::string(key) + "'"});
      return;
    }
    SettingValue v;
    std::string err;
    if (!parseSettingValue(info->type, value, v, err)) {
      diags.push_back({std::string(source), line, false, std::string(key) + ": " + err});
      return;
    }
    if (!seen.insert(std::string(key)).second)
      diags.push_back({std::string(source), line, true, "'" + std::string(key) + "' set twice in this file"});
    into[std::string(key)] = std::move(v);
  });
}

std::string_view defaultSettingsText() { return kDefaults; }

Settings::Settings() {
  std::vector<SettingsDiag> d;
  parseSettings(kDefaults, "settings.default.txt", values_, d);  // a diagnostic here is a build bug; tests check it
}

const SettingValue& Settings::get(const std::string& key) const {
  static const SettingValue none;
  auto it = values_.find(key);
  return it == values_.end() ? none : it->second;
}

int Settings::onChanged(ChangedFn fn) {
  listeners_.emplace_back(nextToken_, std::move(fn));
  return nextToken_++;
}
void Settings::removeOnChanged(int token) {
  for (size_t i = 0; i < listeners_.size(); ++i)
    if (listeners_[i].first == token) {
      listeners_.erase(listeners_.begin() + static_cast<long>(i));
      return;
    }
}

void Settings::apply(std::map<std::string, SettingValue> next, std::vector<SettingsDiag> diags) {
  std::set<std::string> changed;
  for (auto& [k, v] : next) {
    auto it = values_.find(k);
    if (it == values_.end() || !(it->second == v)) changed.insert(k);
  }
  values_ = std::move(next);
  diags_ = std::move(diags);
  if (changed.empty()) return;
  auto copy = listeners_;  // a listener may (un)register
  for (auto& l : copy) l.second(changed);
}

void Settings::loadTexts(std::string_view userText, std::string_view workspaceText) {
  std::map<std::string, SettingValue> next;
  std::vector<SettingsDiag> d;
  parseSettings(kDefaults, "settings.default.txt", next, d);
  d.clear();
  parseSettings(userText, "user settings.txt", next, d);
  parseSettings(workspaceText, "workspace settings.txt", next, d);
  apply(std::move(next), std::move(d));
}

void Settings::load(const std::filesystem::path& workspaceRoot, const std::filesystem::path& userDir) {
  std::map<std::string, SettingValue> next;
  std::vector<SettingsDiag> d;
  parseSettings(kDefaults, "settings.default.txt", next, d);
  d.clear();
  auto layer = [&](const std::filesystem::path& file) {
    if (file.empty()) return;
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return;
    std::string text;
    if (!readFile(file, text)) {
      d.push_back({file.generic_string(), 0, false, "cannot read file"});
      return;
    }
    parseSettings(text, file.generic_string(), next, d);
  };
  if (!userDir.empty()) layer(userDir / "settings.txt");
  if (!workspaceRoot.empty()) layer(workspaceRoot / ".tenfold" / "settings.txt");
  apply(std::move(next), std::move(d));
}

EditorSettings editorSettings(const Settings& s) {
  return {s.getBool("editor.smoothScroll"),   s.getBool("editor.wordWrap"),
          s.getBool("editor.caretBlink"),     s.getBool("editor.highlightCurrentLine"),
          s.getBool("editor.showWhitespace"), s.getInt("editor.caretBlinkMs"),
          s.getInt("editor.wheelLines"),      s.getInt("editor.tabWidth"),
          s.getInt("editor.columnGuide")};
}

// ---- SettingsService ----

SettingsService::SettingsService(Options opt) : opt_(std::move(opt)) { reloadNow(); }
SettingsService::~SettingsService() { stopWatching(); }

std::vector<SettingsDiag> SettingsService::diagnostics() const {
  auto d = settings_.diagnostics();
  d.insert(d.end(), themeDiags_.begin(), themeDiags_.end());
  return d;
}

void SettingsService::loadTheme() {
  themeDiags_.clear();
  std::string fp;
  Theme t = ::tf::ui::loadTheme(settings_.getString("ui.theme"), opt_.workspaceRoot, opt_.userDir, themeDiags_, &fp);
  if (fp == themeFingerprint_) return;
  bool first = themeFingerprint_.empty();
  themeFingerprint_ = fp;
  theme_ = std::move(t);
  if (!first && themeFn_) themeFn_(theme_);
}

void SettingsService::reloadNow() {
  settings_.load(opt_.workspaceRoot, opt_.userDir);
  loadTheme();
}

bool SettingsService::poll() {
  if (!pending_.exchange(false)) return false;
  reloadNow();
  return true;
}

void SettingsService::notifyFileChanged(const std::filesystem::path& p) {
  const std::string name = p.filename().string();
  const std::string parent = p.parent_path().filename().string();
  if (p.extension() != ".txt" || (name != "settings.txt" && parent != "themes")) return;
  pending_ = true;
  if (opt_.wake) opt_.wake();
}

void SettingsService::startWatching() {
  stopWatching();
  auto watch = [&](const std::filesystem::path& dir) {
    FileWatcher::Options o;
    o.debounceMs = opt_.debounceMs;
    o.pollMs = opt_.pollMs;
    o.forcePolling = opt_.forcePolling;
    auto w = std::make_unique<FileWatcher>(o);
    if (w->start(dir, nullptr, [this](FileWatcher::Event e) {
          notifyFileChanged(e.path);
          if (!e.oldPath.empty()) notifyFileChanged(e.oldPath);
          if (e.kind == FileWatcher::Event::Rescan) {
            pending_ = true;
            if (opt_.wake) opt_.wake();
          }
        }))
      watchers_.push_back(std::move(w));
  };
  std::error_code ec;
  if (!opt_.userDir.empty()) {
    std::filesystem::create_directories(opt_.userDir, ec);
    watch(opt_.userDir);
  }
  if (!opt_.workspaceRoot.empty() && std::filesystem::is_directory(opt_.workspaceRoot / ".tenfold", ec))
    watch(opt_.workspaceRoot / ".tenfold");
}

void SettingsService::stopWatching() {
  for (auto& w : watchers_) w->stop();
  watchers_.clear();
}

void SettingsService::applyTo(RootWidget& root, float displayScale) const {
  root.setTheme(theme_.scaled(displayScale));
}

}  // namespace tf::ui
