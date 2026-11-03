#include "ui/settings/KeyBindings.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "ui/commands/CommandNames.h"

namespace tf::ui {

namespace {

const char kDefaults[] =
#include "DefaultKeyBindings.inc"
    ;

std::string lower(std::string_view s) {
  std::string r(s);
  for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return r;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

// Named keys (lower-case). Single printable characters and F1..F24 are handled separately. Everything the 10x
// key map documentation uses is here; the rest are tenfold's own keys (Escape, the keypad Enter, ...).
const std::map<std::string, Key>& keyNames() {
  static const std::map<std::string, Key> m = {
      {"space", Key::Space},
      {"backspace", Key::Backspace},
      {"tab", Key::Tab},
      {"enter", Key::Enter},
      {"kpenter", Key::KpEnter},
      {"escape", Key::Escape},
      {"insert", Key::Insert},
      {"delete", Key::Delete},
      {"home", Key::Home},
      {"end", Key::End},
      {"pageup", Key::PageUp},
      {"pagedown", Key::PageDown},
      {"left", Key::Left},
      {"right", Key::Right},
      {"up", Key::Up},
      {"down", Key::Down},
      {"comma", static_cast<Key>(',')},
      {"cancel", Key::Pause},  // 10x's name for Pause/Break (Control+Break cancels a build)
      {"appskey", Key::Menu},
      {"capslock", Key::CapsLock},
      {"printscreen", Key::PrintScreen},
  };
  return m;
}

bool parseKeyName(std::string_view tok, Key& out) {
  if (tok.size() == 1) {
    unsigned char c = static_cast<unsigned char>(tok[0]);
    if (c > ' ' && c < 127 && c != ',') {  // a comma separates chords: it is written Comma
      out = static_cast<Key>(std::toupper(c));
      return true;
    }
  }
  const std::string l = lower(tok);
  if (l.size() >= 2 && l.size() <= 3 && l[0] == 'f' && std::isdigit(static_cast<unsigned char>(l[1]))) {
    char* end = nullptr;
    long n = std::strtol(l.c_str() + 1, &end, 10);
    if (*end == 0 && n >= 1 && n <= 24) {
      out = static_cast<Key>(static_cast<uint32_t>(Key::F1) + static_cast<uint32_t>(n - 1));
      return true;
    }
    return false;
  }
  auto it = keyNames().find(l);
  if (it == keyNames().end()) return false;
  out = it->second;
  return true;
}

bool parseMod(std::string_view tok, uint8_t& m) {
  const std::string l = lower(tok);
  if (l == "control" || l == "ctrl") m |= ModCtrl;
  else if (l == "shift") m |= ModShift;
  else if (l == "alt") m |= ModAlt;
  else return false;
  return true;
}

bool validIdent(std::string_view s) {
  if (s.empty() || std::isdigit(static_cast<unsigned char>(s[0]))) return false;
  for (char c : s)
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.') return false;
  return true;
}

std::vector<std::string_view> splitWords(std::string_view s) {
  std::vector<std::string_view> w;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    size_t j = i;
    while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j]))) ++j;
    if (j > i) w.push_back(s.substr(i, j - i));
    i = j;
  }
  return w;
}

bool sameChords(const std::vector<KeyChord>& a, const std::vector<KeyChord>& b) { return a == b; }

}  // namespace

std::string formatDiag(const KeyBindingDiag& d) {
  std::string s = d.source;
  if (d.line > 0) s += ":" + std::to_string(d.line);
  return s + (d.warning ? ": warning: " : ": error: ") + d.message;
}

bool parseKeyChord(std::string_view text, KeyChord& out, std::string& error) {
  const std::vector<std::string_view> words = splitWords(text);
  if (words.empty()) {
    error = "empty key";
    return false;
  }
  uint8_t m = ModNone;
  for (size_t i = 0; i + 1 < words.size(); ++i)
    if (!parseMod(words[i], m)) {
      error = "unknown modifier '" + std::string(words[i]) + "' (use Control, Shift or Alt)";
      return false;
    }
  const std::string_view keyTok = words.back();
  const std::string l = lower(keyTok);
  if (l.compare(0, 5, "mouse") == 0) {
    if (l == "mousemiddlebutton") out = chordFromMouse(MouseButton::Middle, m);
    else if (l == "mousebackbutton") out = chordFromMouse(MouseButton::X1, m);
    else if (l == "mouseforwardbutton") out = chordFromMouse(MouseButton::X2, m);
    else {
      error = "mouse button '" + std::string(keyTok) +
              "' is not bindable (use MouseMiddleButton, MouseBackButton or MouseForwardButton)";
      return false;
    }
    return true;
  }
  Key k;
  if (!parseKeyName(keyTok, k)) {
    error = "unknown key '" + std::string(keyTok) + "'";
    if (keyTok.size() > 1 && keyTok.find('+') != std::string_view::npos)
      error += " (separate modifiers and key with spaces, e.g. 'Control Shift F')";
    return false;
  }
  out = chordFromKey(k, m);
  return true;
}

bool parseKeyChords(std::string_view text, std::vector<KeyChord>& out, std::string& error) {
  out.clear();
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t comma = text.find(',', pos);
    std::string_view part =
        trim(text.substr(pos, comma == std::string_view::npos ? std::string_view::npos : comma - pos));
    if (part.empty()) {
      error = text.find(',') == std::string_view::npos ? "empty key" : "empty chord (a comma key is written Comma)";
      return false;
    }
    KeyChord c;
    if (!parseKeyChord(part, c, error)) return false;
    out.push_back(c);
    if (comma == std::string_view::npos) break;
    pos = comma + 1;
  }
  return true;
}

void parseKeyBindings(std::string_view text, std::string_view source, KeyMap& into,
                      std::vector<KeyBindingDiag>& diags) {
  std::set<std::string> seen;                                       // "context|chords" of this text
  std::vector<std::pair<std::vector<KeyChord>, std::string>> mine;  // bindings this text added
  int lineNo = 0;
  size_t pos = 0;
  if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) pos = 3;  // UTF-8 BOM
  while (pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
    pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
    ++lineNo;
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    auto diag = [&](bool warning, std::string msg) {
      diags.push_back({std::string(source), lineNo, warning, std::move(msg)});
    };

    // The separator is the first colon followed by whitespace (or the end of the line): 10x requires the space.
    size_t colon = std::string_view::npos;
    for (size_t i = 0; i < line.size(); ++i)
      if (line[i] == ':' && (i + 1 == line.size() || std::isspace(static_cast<unsigned char>(line[i + 1])))) {
        colon = i;
        break;
      }
    if (colon == std::string_view::npos) {
      if (line.find(" = ") != std::string_view::npos)
        diag(false,
             "the old tenfold key binding format ('keys = Command') is no longer supported; write "
             "'<keys>: <Command>' like 10x, e.g. 'Control Shift F: Search'");
      else diag(false, "expected '<keys>: <command>' (a space is required after the colon)");
      continue;
    }
    const std::string_view lhs = trim(line.substr(0, colon)), rhs = trim(line.substr(colon + 1));
    if (lhs.empty()) {
      diag(false, "missing key before ':'");
      continue;
    }
    KeyBinding b;
    std::string err;
    if (!parseKeyChords(lhs, b.chords, err)) {
      diag(false, err);
      continue;
    }

    bool unbind = rhs.empty();
    if (!unbind) {
      size_t sp = 0;
      while (sp < rhs.size() && !std::isspace(static_cast<unsigned char>(rhs[sp]))) ++sp;
      const std::string_view cmd = rhs.substr(0, sp), arg = trim(rhs.substr(sp));
      // A value that is not "Name [argument]" is a Python statement (10x allows them): `Name()`, `x = 1`, `a.b(c)`.
      const bool python =
          !validIdent(cmd) || (!arg.empty() && std::string_view("(=.[").find(arg[0]) != std::string_view::npos);
      if (python) {
        b.command = kPythonCommand;
        b.arg = std::string(rhs);
        b.written = std::string(rhs);
        b.context = "global";
        diag(true,
             "Python statements are not supported ('" + std::string(rhs) + "'); the key is bound but does nothing");
      } else {
        const ResolvedCommand rc = resolveCommandName(cmd);
        b.command = rc.id;
        b.arg = std::string(arg);
        b.written = std::string(cmd);
        b.context = rc.context;
      }
    }

    if (!seen.insert(unbind ? "|" + formatChords(b.chords) : b.context + "|" + formatChords(b.chords)).second)
      diag(true, "duplicate binding for " + formatChords(b.chords) + "; the later one wins");

    // Replace what earlier layers (or earlier lines) bound to these keys, in every context.
    std::vector<std::pair<std::vector<KeyChord>, std::string>> drop;
    for (const KeyBinding& e : into.bindings())
      if (sameChords(e.chords, b.chords) && (unbind || e.context != b.context)) {
        const bool fromThisText =
            std::find(mine.begin(), mine.end(), std::make_pair(e.chords, e.context)) != mine.end();
        if (unbind || !fromThisText) drop.emplace_back(e.chords, e.context);
      }
    for (auto& [c, ctx] : drop) into.remove(c, ctx);
    if (unbind) {
      std::erase_if(mine, [&](const auto& m) { return m.first == b.chords; });
      continue;
    }
    mine.emplace_back(b.chords, b.context);
    into.add(std::move(b));
  }
}

void loadKeyBindingFile(const std::filesystem::path& file, KeyMap& into, std::vector<KeyBindingDiag>& diags) {
  std::error_code ec;
  if (!std::filesystem::exists(file, ec)) return;
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    diags.push_back({file.string(), 0, false, "cannot read file"});
    return;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  parseKeyBindings(ss.str(), file.string(), into, diags);
}

std::string_view defaultKeyBindingsText() { return kDefaults; }

std::filesystem::path userConfigDir() {
  auto env = [](const char* n) -> std::string {
    const char* v = std::getenv(n);
    return v ? v : "";
  };
#if defined(_WIN32)
  std::string base = env("APPDATA");
#elif defined(__APPLE__)
  std::string home = env("HOME");
  std::string base = home.empty() ? "" : home + "/Library/Application Support";
#else
  std::string base = env("XDG_CONFIG_HOME");
  if (base.empty()) {
    std::string home = env("HOME");
    if (!home.empty()) base = home + "/.config";
  }
#endif
  return base.empty() ? std::filesystem::path() : std::filesystem::path(base) / "tenfold";
}

std::filesystem::path default10xKeyMappingsPath() {
#if defined(_WIN32)
  const char* v = std::getenv("APPDATA");
  if (v && *v) return std::filesystem::path(v) / "10x" / "Settings" / kKeyMappingsFileName;
#endif
  return {};
}

namespace {

// The old format lived in keybindings.txt; it is not read any more, but a leftover file deserves a pointer.
void warnLegacyFile(const std::filesystem::path& dir, std::vector<KeyBindingDiag>& diags) {
  std::error_code ec;
  const std::filesystem::path old = dir / "keybindings.txt";
  if (std::filesystem::exists(old, ec))
    diags.push_back({old.string(), 0, true,
                     "the old tenfold key binding file is no longer read; move your bindings to " +
                         std::string(kKeyMappingsFileName) + " in 10x format ('Control Shift F: Search')"});
}

}  // namespace

KeyMap loadKeyBindings(const KeyLoadOptions& opt, std::vector<KeyBindingDiag>& diags) {
  KeyMap m;
  parseKeyBindings(defaultKeyBindingsText(), "KeyMappings.default.10x_settings", m, diags);
  if (opt.import10x && !opt.import10xFile.empty()) loadKeyBindingFile(opt.import10xFile, m, diags);
  if (!opt.userDir.empty()) {
    loadKeyBindingFile(opt.userDir / kKeyMappingsFileName, m, diags);
    warnLegacyFile(opt.userDir, diags);
  }
  if (!opt.workspaceRoot.empty()) {
    loadKeyBindingFile(opt.workspaceRoot / ".tenfold" / kKeyMappingsFileName, m, diags);
    warnLegacyFile(opt.workspaceRoot / ".tenfold", diags);
  }
  return m;
}

KeyMap loadKeyBindings(const std::filesystem::path& workspaceRoot, const std::filesystem::path& userDir,
                       std::vector<KeyBindingDiag>& diags) {
  KeyLoadOptions o;
  o.workspaceRoot = workspaceRoot;
  o.userDir = userDir;
  o.import10x = false;
  return loadKeyBindings(o, diags);
}

}  // namespace tf::ui
