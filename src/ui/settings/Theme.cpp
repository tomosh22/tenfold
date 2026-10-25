#include "ui/settings/Theme.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace tf::ui {

namespace {

const char kDark[] =
#include "DefaultThemeDark.inc"
    ;
const char kLight[] =
#include "DefaultThemeLight.inc"
    ;
const char kVsDark[] =
#include "DefaultThemeVsDark.inc"
    ;

struct ColorMember {
  const char* name;
  gfx::Color Theme::*ptr;
  bool optional = false;  // transparent default = legacy look; shipped themes need not define it
};
const std::vector<ColorMember>& colorMembers() {
  static const std::vector<ColorMember> m = {
      {"background", &Theme::background},
      {"panelBackground", &Theme::panelBackground},
      {"tabStripBackground", &Theme::tabStripBackground},
      {"tabActive", &Theme::tabActive},
      {"tabInactive", &Theme::tabInactive},
      {"tabHover", &Theme::tabHover},
      {"tabAccent", &Theme::tabAccent},
      {"tabInsert", &Theme::tabInsert},
      {"text", &Theme::text},
      {"textDim", &Theme::textDim},
      {"dirtyMarker", &Theme::dirtyMarker},
      {"closeHover", &Theme::closeHover},
      {"splitter", &Theme::splitter},
      {"splitterHot", &Theme::splitterHot},
      {"statusBackground", &Theme::statusBackground},
      {"statusText", &Theme::statusText},
      {"statusHover", &Theme::statusHover},
      {"gutterBackground", &Theme::gutterBackground},
      {"gutterText", &Theme::gutterText},
      {"currentLine", &Theme::currentLine},
      {"selection", &Theme::selection},
      {"caret", &Theme::caret},
      {"columnGuide", &Theme::columnGuide},
      {"whitespace", &Theme::whitespace},
      {"panelBorder", &Theme::panelBorder},
      {"diagnosticError", &Theme::diagnosticError},
      {"diagnosticWarning", &Theme::diagnosticWarning},
      {"diagnosticInfo", &Theme::diagnosticInfo},
      {"buildError", &Theme::buildError},
      {"buildWarning", &Theme::buildWarning},
      {"matchMarker", &Theme::matchMarker},
      {"scrollbarTrack", &Theme::scrollbarTrack},
      {"scrollbarThumb", &Theme::scrollbarThumb},
      {"bracketMatch", &Theme::bracketMatch},
      {"editorBackground", &Theme::editorBackground, true},
      {"currentLineFill", &Theme::currentLineFill, true},
      {"currentLineBorder", &Theme::currentLineBorder, true},
      {"selectionActive", &Theme::selectionActive, true},
      {"selectionInactive", &Theme::selectionInactive, true},
      {"findHighlight", &Theme::findHighlight, true},
      {"findHighlightCurrent", &Theme::findHighlightCurrent, true},
      {"editorCaret", &Theme::editorCaret, true},
      {"tabActiveText", &Theme::tabActiveText, true},
      {"tabInactiveText", &Theme::tabInactiveText, true},
      {"popupBackground", &Theme::popupBackground, true},
      {"popupBorder", &Theme::popupBorder, true},
      {"debugBreakpoint", &Theme::debugBreakpoint, true},
      {"debugCurrentLine", &Theme::debugCurrentLine, true},
      {"debugCurrentArrow", &Theme::debugCurrentArrow, true},
      {"statusBackgroundDebugging", &Theme::statusBackgroundDebugging, true},
      {"statusTextDebugging", &Theme::statusTextDebugging, true},
      {"diffAdded", &Theme::diffAdded, true},
      {"diffModified", &Theme::diffModified, true},
      {"diffDeleted", &Theme::diffDeleted, true},
      {"diffBlockBackground", &Theme::diffBlockBackground, true},
      {"diffBlockText", &Theme::diffBlockText, true},
      {"diffBlockHeader", &Theme::diffBlockHeader, true},
      {"tabVcsModified", &Theme::tabVcsModified, true},
      {"tabVcsAdded", &Theme::tabVcsAdded, true},
      {"tabVcsCheckedOut", &Theme::tabVcsCheckedOut, true},
      {"tabVcsReadOnly", &Theme::tabVcsReadOnly, true},
  };
  return m;
}

const std::vector<std::string> kLexical = {"keyword", "identifier",   "number",   "string",     "char",
                                           "comment", "preprocessor", "operator", "punctuation"};
// Order matters for nothing but readability; "type" is the generic one.
const std::vector<std::string> kSemantic = {"namespace", "type",       "class",         "struct",    "interface",
                                            "enum",      "enumMember", "typeParameter", "concept",   "function",
                                            "method",    "property",   "variable",      "parameter", "macro",
                                            "event",     "decorator",  "modifier",      "regexp"};

bool contains(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

const std::vector<std::string>& themeColorNames() {
  static const std::vector<std::string> n = [] {
    std::vector<std::string> r;
    for (auto& m : colorMembers()) r.push_back(m.name);
    return r;
  }();
  return n;
}
const std::vector<std::string>& themeTokenKinds() {
  static const std::vector<std::string> k = [] {
    std::vector<std::string> r = kLexical;
    r.insert(r.end(), kSemantic.begin(), kSemantic.end());
    return r;
  }();
  return k;
}
const std::vector<std::string>& themeTokenModifiers() {
  static const std::vector<std::string> m = {"declaration",   "definition",    "readonly", "static",
                                             "deprecated",    "abstract",      "async",    "modification",
                                             "documentation", "defaultLibrary"};
  return m;
}

std::vector<std::string> requiredThemeKeys() {
  std::vector<std::string> r;
  for (auto& m : colorMembers())
    if (!m.optional) r.push_back(std::string("color.") + m.name);
  for (auto& n : kLexical) r.push_back("token." + n);
  for (const char* n : {"namespace", "type", "class", "enum", "enumMember", "typeParameter", "concept", "function",
                        "method", "property", "variable", "parameter", "macro"})
    r.push_back(std::string("token.") + n);
  return r;
}

std::vector<std::string> themeOptionalColorNames() {
  std::vector<std::string> r;
  for (auto& m : colorMembers())
    if (m.optional) r.push_back(m.name);
  return r;
}

Theme parseTheme(std::string_view text, std::string_view source, std::vector<SettingsDiag>& diags, const Theme& base) {
  Theme t = base;
  std::set<std::string> seen;
  parseKeyValueLines(text, source, diags, [&](int line, std::string_view key, std::string_view value) {
    auto err = [&](bool warning, std::string msg) {
      diags.push_back({std::string(source), line, warning, std::move(msg)});
    };
    std::string k(key);
    if (!seen.insert(k).second) err(true, "'" + k + "' set twice in this file");
    gfx::Color c;
    std::string e;
    if (k.rfind("color.", 0) == 0) {
      const ColorMember* m = nullptr;
      for (auto& cm : colorMembers())
        if (k.compare(6, std::string::npos, cm.name) == 0) m = &cm;
      if (!m) return err(false, "unknown theme colour '" + k + "'");
      if (!parseColor(value, c, e)) return err(false, k + ": " + e);
      t.*(m->ptr) = c;
      return;
    }
    if (k.rfind("token.", 0) == 0) {
      std::string rest = k.substr(6), kind = rest, mod;
      if (size_t dot = rest.find('.'); dot != std::string::npos) {
        kind = rest.substr(0, dot);
        mod = rest.substr(dot + 1);
      }
      if (!contains(themeTokenKinds(), kind)) return err(false, "unknown token kind in '" + k + "'");
      if (!mod.empty() && !contains(themeTokenModifiers(), mod))
        return err(false, "unknown token modifier in '" + k + "'");
      if (!parseColor(value, c, e)) return err(false, k + ": " + e);
      t.tokens[rest] = c;
      return;
    }
    err(false, "unknown theme key '" + k + "' (expected color.* or token.*)");
  });
  return t;
}

std::string_view builtinThemeText(std::string_view name) {
  if (name == "dark") return kDark;
  if (name == "light") return kLight;
  if (name == "vs-dark") return kVsDark;
  return {};
}

std::filesystem::path findThemeFile(std::string_view name, const std::filesystem::path& workspaceRoot,
                                    const std::filesystem::path& userDir) {
  if (name.empty() || name.find_first_of("/\\") != std::string_view::npos || name == "..") return {};
  std::error_code ec;
  std::string file = std::string(name) + ".txt";
  for (const auto& p : {workspaceRoot.empty() ? std::filesystem::path() : workspaceRoot / ".tenfold" / "themes" / file,
                        userDir.empty() ? std::filesystem::path() : userDir / "themes" / file})
    if (!p.empty() && std::filesystem::is_regular_file(p, ec)) return p;
  return {};
}

Theme loadTheme(std::string_view name, const std::filesystem::path& workspaceRoot, const std::filesystem::path& userDir,
                std::vector<SettingsDiag>& diags, std::string* fingerprint) {
  std::string text, source;
  std::filesystem::path file = findThemeFile(name, workspaceRoot, userDir);
  if (!file.empty()) {
    std::ifstream f(file, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    text = ss.str();
    source = file.generic_string();
  } else if (!builtinThemeText(name).empty()) {
    text = std::string(builtinThemeText(name));
    source = std::string(name) + " (built-in)";
  } else {
    diags.push_back({"settings", 0, false, "unknown theme '" + std::string(name) + "'; using dark"});
    text = std::string(kDark);
    source = "dark (built-in)";
  }
  if (fingerprint) *fingerprint = source + "\n" + text;
  return parseTheme(text, source, diags);
}

}  // namespace tf::ui
