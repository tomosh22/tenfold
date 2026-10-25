#include "ui/shell/FontSetup.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace tf::ui {

namespace fs = std::filesystem;

namespace {
std::string normalize(const std::string& s) {
  std::string r;
  for (char c : s)
    if (std::isalnum(static_cast<unsigned char>(c)))
      r += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return r;
}
}  // namespace

std::string resolveFontFile(const std::string& name) {
  std::error_code ec;
  if (name.empty()) return {};
  if (fs::is_regular_file(name, ec)) return name;
  std::string want = normalize(name);
  if (want == "consolas") want = "consola";  // the Windows file name
  std::vector<fs::path> roots;
#if defined(_WIN32)
  roots = {"C:\\Windows\\Fonts"};
  if (const char* l = std::getenv("LOCALAPPDATA")) roots.push_back(fs::path(l) / "Microsoft" / "Windows" / "Fonts");
#elif defined(__APPLE__)
  roots = {"/System/Library/Fonts", "/Library/Fonts"};
  if (const char* h = std::getenv("HOME")) roots.push_back(fs::path(h) / "Library" / "Fonts");
#else
  roots = {"/usr/share/fonts", "/usr/local/share/fonts"};
  if (const char* h = std::getenv("HOME")) {
    roots.push_back(fs::path(h) / ".local/share/fonts");
    roots.push_back(fs::path(h) / ".fonts");
  }
#endif
  std::string best, bestStem;
  for (const fs::path& root : roots) {
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
      const fs::path& p = it->path();
      const std::string ext = normalize(p.extension().string());
      if (ext != "ttf" && ext != "otf" && ext != "ttc") continue;
      const std::string stem = normalize(p.stem().string());
      if (stem.compare(0, want.size(), want) != 0) continue;
      if (best.empty() || stem.size() < bestStem.size()) best = p.string(), bestStem = stem;
    }
    ec.clear();
  }
  return best;
}

std::unique_ptr<gfx::TextRenderer> makeEditorTextRenderer(const Settings& s, float dpiScale, std::string* err) {
  const FontSpec& spec = s.getFont("font.editor");
  const float pt = spec.sizePt > 0 ? spec.sizePt : static_cast<float>(s.getFloat("font.size"));
  const float px = (pt > 0 ? pt : 11.0f) * 4.0f / 3.0f;
  std::vector<std::string> names;
  if (!spec.family.empty()) names.push_back(spec.family);
  const std::string& fb = s.getString("font.fallbacks");
  for (size_t from = 0; from <= fb.size();) {
    size_t comma = fb.find(',', from);
    if (comma == std::string::npos) comma = fb.size();
    std::string n = fb.substr(from, comma - from);
    n.erase(0, n.find_first_not_of(" \t"));
    n.erase(n.find_last_not_of(" \t") + 1);
    if (!n.empty()) names.push_back(n);
    from = comma + 1;
  }
  for (const std::string& n : names) {
    const std::string file = resolveFontFile(n);
    if (!file.empty())
      if (auto tr = gfx::TextRenderer::create(file, px, dpiScale)) return tr;
  }
  return gfx::TextRenderer::create("", px, dpiScale, err);  // the platform's usual monospace font
}

}  // namespace tf::ui
