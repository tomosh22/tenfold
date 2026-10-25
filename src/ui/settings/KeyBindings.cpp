#include "ui/settings/KeyBindings.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

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

// Named keys (lower-case). Single printable characters and F1..F24 are handled separately.
const std::map<std::string, Key>& keyNames() {
  static const std::map<std::string, Key> m = [] {
    std::map<std::string, Key> r = {
        {"space", Key::Space},
        {"backspace", Key::Backspace},
        {"tab", Key::Tab},
        {"enter", Key::Enter},
        {"return", Key::Enter},
        {"kpenter", Key::KpEnter},
        {"numenter", Key::KpEnter},
        {"escape", Key::Escape},
        {"esc", Key::Escape},
        {"insert", Key::Insert},
        {"ins", Key::Insert},
        {"delete", Key::Delete},
        {"del", Key::Delete},
        {"home", Key::Home},
        {"end", Key::End},
        {"pageup", Key::PageUp},
        {"pgup", Key::PageUp},
        {"pagedown", Key::PageDown},
        {"pgdn", Key::PageDown},
        {"pgdown", Key::PageDown},
        {"left", Key::Left},
        {"right", Key::Right},
        {"up", Key::Up},
        {"down", Key::Down},
        {"capslock", Key::CapsLock},
        {"menu", Key::Menu},
        {"printscreen", Key::PrintScreen},
        {"pause", Key::Pause},
    };
    const std::pair<const char*, char> punct[] = {
        {"minus", '-'},       {"plus", '+'},       {"comma", ','},        {"period", '.'},  {"dot", '.'},
        {"slash", '/'},       {"backslash", '\\'}, {"semicolon", ';'},    {"quote", '\''},  {"apostrophe", '\''},
        {"backquote", '`'},   {"grave", '`'},      {"equals", '='},       {"equal", '='},   {"lbracket", '['},
        {"leftbracket", '['}, {"rbracket", ']'},   {"rightbracket", ']'}, {"numplus", '+'}, {"numminus", '-'},
        {"nummultiply", '*'}, {"numdivide", '/'},  {"numdecimal", '.'}};
    for (auto& [n, c] : punct) r[n] = static_cast<Key>(c);
    for (int d = 0; d <= 9; ++d) r["num" + std::to_string(d)] = static_cast<Key>('0' + d);  // keypad = same key
    return r;
  }();
  return m;
}

bool parseKeyName(std::string_view tok, Key& out) {
  std::string l = lower(tok);
  if (tok.size() == 1) {
    unsigned char c = static_cast<unsigned char>(tok[0]);
    if (c > ' ' && c < 127) {
      out = static_cast<Key>(std::toupper(c));
      return true;
    }
  }
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
  std::string l = lower(tok);
  if (l == "ctrl" || l == "control") m |= ModCtrl;
  else if (l == "shift") m |= ModShift;
  else if (l == "alt" || l == "option") m |= ModAlt;
  else if (l == "super" || l == "win" || l == "cmd" || l == "meta") m |= ModSuper;
  else return false;
  return true;
}

bool validIdent(std::string_view s, bool dots) {
  if (s.empty()) return false;
  for (char c : s)
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && !(dots && c == '.')) return false;
  return true;
}

}  // namespace

std::string formatDiag(const KeyBindingDiag& d) {
  std::string s = d.source;
  if (d.line > 0) s += ":" + std::to_string(d.line);
  return s + (d.warning ? ": warning: " : ": error: ") + d.message;
}

bool parseKeyChord(std::string_view text, KeyChord& out, std::string& error) {
  std::string_view s = trim(text);
  if (s.empty()) {
    error = "empty key";
    return false;
  }
  // "+" is both the separator and a key: a trailing '+' is the Plus key ("Ctrl++", "+").
  std::string_view mods = s, keyTok;
  if (s.back() == '+') {
    keyTok = "+";
    mods = s.substr(0, s.size() - 1);
    if (!mods.empty()) {
      if (mods.back() != '+') {
        error = "malformed chord '" + std::string(s) + "'";
        return false;
      }
      mods.remove_suffix(1);
    }
  } else {
    size_t p = s.rfind('+');
    keyTok = p == std::string_view::npos ? s : s.substr(p + 1);
    mods = p == std::string_view::npos ? std::string_view{} : s.substr(0, p);
  }
  uint8_t m = ModNone;
  while (!mods.empty()) {
    size_t p = mods.find('+');
    std::string_view t = mods.substr(0, p);
    if (!parseMod(t, m)) {
      error = "unknown modifier '" + std::string(t) + "'";
      return false;
    }
    mods = p == std::string_view::npos ? std::string_view{} : mods.substr(p + 1);
  }
  std::string l = lower(keyTok);
  if (l.size() == 6 && l.compare(0, 5, "mouse") == 0) {
    if (l[5] >= '4' && l[5] <= '5') {
      out = chordFromMouse(static_cast<MouseButton>(l[5] - '1'), m);
      return true;
    }
    error = "mouse button '" + std::string(keyTok) + "' is not bindable (use Mouse4 or Mouse5)";
    return false;
  }
  Key k;
  if (!parseKeyName(keyTok, k)) {
    error = "unknown key '" + std::string(keyTok) + "'";
    return false;
  }
  out = chordFromKey(k, m);
  return true;
}

void parseKeyBindings(std::string_view text, std::string_view source, KeyMap& into,
                      std::vector<KeyBindingDiag>& diags) {
  std::set<std::string> seen;
  int lineNo = 0;
  size_t pos = 0;
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

    KeyBinding b;
    if (line[0] == '[') {
      size_t close = line.find(']');
      if (close == std::string_view::npos) {
        diag(false, "missing ']' after context");
        continue;
      }
      b.context = lower(trim(line.substr(1, close - 1)));
      if (!validIdent(b.context, false)) {
        diag(false, "invalid context name '" + b.context + "'");
        continue;
      }
      line = trim(line.substr(close + 1));
    }
    // The separator is the first '=' set off by whitespace, so "Ctrl+= = Cmd" and "= = Cmd" work.
    size_t eq = std::string_view::npos;
    for (size_t i = 0; i < line.size(); ++i)
      if (line[i] == '=' && i > 0 && std::isspace(static_cast<unsigned char>(line[i - 1])) &&
          (i + 1 == line.size() || std::isspace(static_cast<unsigned char>(line[i + 1])))) {
        eq = i;
        break;
      }
    if (eq == std::string_view::npos) {
      diag(false, "expected '<keys> = <command>'");
      continue;
    }
    std::string_view lhs = trim(line.substr(0, eq)), rhs = trim(line.substr(eq + 1));
    if (lhs.empty()) {
      diag(false, "missing key before '='");
      continue;
    }
    bool ok = true;
    for (std::istringstream ss{std::string(lhs)}; ok;) {
      std::string tok;
      if (!(ss >> tok)) break;
      KeyChord c;
      std::string err;
      if (parseKeyChord(tok, c, err)) {
        b.chords.push_back(c);
      } else {
        diag(false, err);
        ok = false;
      }
    }
    if (!ok) continue;
    if (rhs.empty()) {
      diag(false, "missing command after '='");
      continue;
    }
    size_t sp = 0;
    while (sp < rhs.size() && !std::isspace(static_cast<unsigned char>(rhs[sp]))) ++sp;
    b.command = std::string(rhs.substr(0, sp));
    b.arg = std::string(trim(rhs.substr(sp)));
    const bool unbind = b.command == "-";
    if (!unbind && !validIdent(b.command, true)) {
      diag(false, "invalid command id '" + b.command + "'");
      continue;
    }
    if (unbind && !b.arg.empty()) {
      diag(false, "'-' (unbind) takes no argument");
      continue;
    }
    if (!seen.insert(b.context + "|" + formatChords(b.chords)).second)
      diag(true, "duplicate binding for " + formatChords(b.chords) + " [" + b.context + "]; the later one wins");
    if (unbind) into.remove(b.chords, b.context);
    else into.add(std::move(b));
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

KeyMap loadKeyBindings(const std::filesystem::path& workspaceRoot, const std::filesystem::path& userDir,
                       std::vector<KeyBindingDiag>& diags) {
  KeyMap m;
  parseKeyBindings(defaultKeyBindingsText(), "keybindings.default.txt", m, diags);
  if (!userDir.empty()) loadKeyBindingFile(userDir / "keybindings.txt", m, diags);
  if (!workspaceRoot.empty()) loadKeyBindingFile(workspaceRoot / ".tenfold" / "keybindings.txt", m, diags);
  return m;
}

}  // namespace tf::ui
