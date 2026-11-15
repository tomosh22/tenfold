#include "workspace/ProjectEdit.h"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "core/PathUtil.h"

namespace tf::projedit {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool hasExt(const fs::path& p, std::initializer_list<const char*> exts) {
  const std::string e = lower(pathUtf8(p.extension()));
  for (const char* x : exts)
    if (e == x) return true;
  return false;
}

// ---- a tolerant scan of the elements of an XML text (positions only; nothing is re-serialised) ----

struct El {
  std::string name;
  size_t tagStart = 0;    // the '<' of the start tag
  size_t openEnd = 0;     // one past the '>' of the start tag
  size_t closeStart = 0;  // the '<' of the end tag (== end for a self-closing element)
  size_t end = 0;         // one past the element
  int parent = -1;
  bool selfClosing = false;
  std::string attrs;  // the text between the name and '>' (without the closing '/')
};

bool isNameChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':' || c == '-' || c == '.';
}

bool scan(const std::string& x, std::vector<El>& out) {
  std::vector<int> stack;
  size_t i = 0;
  const size_t n = x.size();
  while (i < n) {
    const size_t lt = x.find('<', i);
    if (lt == std::string::npos) break;
    auto skipTo = [&](const char* close, size_t len) {
      const size_t e = x.find(close, lt + len);
      if (e == std::string::npos) return false;
      i = e + std::char_traits<char>::length(close);
      return true;
    };
    if (x.compare(lt, 4, "<!--") == 0) {
      if (!skipTo("-->", 4)) return false;
      continue;
    }
    if (x.compare(lt, 9, "<![CDATA[") == 0) {
      if (!skipTo("]]>", 9)) return false;
      continue;
    }
    if (x.compare(lt, 2, "<?") == 0) {
      if (!skipTo("?>", 2)) return false;
      continue;
    }
    if (x.compare(lt, 2, "<!") == 0) {
      if (!skipTo(">", 2)) return false;
      continue;
    }
    if (lt + 1 >= n) return false;
    if (x[lt + 1] == '/') {
      const size_t gt = x.find('>', lt);
      if (gt == std::string::npos || stack.empty()) return false;
      size_t a = lt + 2, b = gt;
      while (b > a && std::isspace(static_cast<unsigned char>(x[b - 1]))) --b;
      El& e = out[static_cast<size_t>(stack.back())];
      if (x.compare(a, b - a, e.name) != 0 || b - a != e.name.size()) return false;
      e.closeStart = lt;
      e.end = gt + 1;
      stack.pop_back();
      i = gt + 1;
      continue;
    }
    size_t ne = lt + 1;
    while (ne < n && isNameChar(x[ne])) ++ne;
    if (ne == lt + 1) return false;
    size_t gt = ne;
    char quote = 0;
    for (; gt < n; ++gt) {  // the '>' that is not inside an attribute value
      const char c = x[gt];
      if (quote) {
        if (c == quote) quote = 0;
      } else if (c == '"' || c == '\'') {
        quote = c;
      } else if (c == '>') {
        break;
      } else if (c == '<' || (c == '/' && (gt + 1 >= n || x[gt + 1] != '>'))) {
        return false;  // not a start tag of a project file
      }
    }
    if (gt >= n) return false;
    El e;
    e.name = x.substr(lt + 1, ne - lt - 1);
    e.tagStart = lt;
    e.openEnd = gt + 1;
    e.selfClosing = x[gt - 1] == '/';
    e.attrs = x.substr(ne, gt - ne - (e.selfClosing ? 1 : 0));
    e.parent = stack.empty() ? -1 : stack.back();
    if (e.selfClosing) {
      e.closeStart = e.end = gt + 1;
    } else {
      stack.push_back(static_cast<int>(out.size()));
    }
    out.push_back(std::move(e));
    i = gt + 1;
  }
  return stack.empty() && !out.empty();
}

// The value of attribute `name` in an attribute text, "" when absent.
std::string attrValue(const std::string& attrs, const std::string& name) {
  size_t at = 0;
  while ((at = attrs.find(name, at)) != std::string::npos) {
    const bool startOk = at == 0 || std::isspace(static_cast<unsigned char>(attrs[at - 1]));
    size_t p = at + name.size();
    at = p;
    if (!startOk) continue;
    while (p < attrs.size() && std::isspace(static_cast<unsigned char>(attrs[p]))) ++p;
    if (p >= attrs.size() || attrs[p] != '=') continue;
    ++p;
    while (p < attrs.size() && std::isspace(static_cast<unsigned char>(attrs[p]))) ++p;
    if (p >= attrs.size() || (attrs[p] != '"' && attrs[p] != '\'')) continue;
    const size_t e = attrs.find(attrs[p], p + 1);
    if (e == std::string::npos) return {};
    return attrs.substr(p + 1, e - p - 1);
  }
  return {};
}

bool hasAttr(const std::string& attrs, const std::string& name) {
  size_t at = 0;
  while ((at = attrs.find(name, at)) != std::string::npos) {
    const bool startOk = at == 0 || std::isspace(static_cast<unsigned char>(attrs[at - 1]));
    const size_t p = at + name.size();
    at = p;
    if (startOk && p < attrs.size() && (attrs[p] == '=' || std::isspace(static_cast<unsigned char>(attrs[p]))))
      return true;
  }
  return false;
}

std::string xmlEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default: o += c;
    }
  }
  return o;
}

std::string xmlUnescape(std::string s) {
  static const std::pair<const char*, char> ents[] = {
      {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}, {"&amp;", '&'}};
  for (const auto& [e, c] : ents) {
    const std::string needle = e;
    for (size_t at = 0; (at = s.find(needle, at)) != std::string::npos; ++at) s.replace(at, needle.size(), 1, c);
  }
  return s;
}

std::string normInclude(std::string v) {
  v = xmlUnescape(std::move(v));
  for (char& c : v) {
    if (c == '/') c = '\\';
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return v;
}

// Leading whitespace of the line that holds `pos`, and whether only whitespace precedes `pos` on that line.
std::string lineIndent(const std::string& x, size_t pos, bool* own) {
  size_t b = pos;
  while (b > 0 && (x[b - 1] == ' ' || x[b - 1] == '\t')) --b;
  *own = b == 0 || x[b - 1] == '\n' || x[b - 1] == '\r';
  return x.substr(b, pos - b);
}

std::string eolOf(const std::string& x) {
  size_t crlf = 0, lf = 0;
  for (size_t i = 0; i < x.size(); ++i)
    if (x[i] == '\n') (i > 0 && x[i - 1] == '\r' ? crlf : lf)++;
  return lf > crlf ? "\n" : "\r\n";  // a file without a newline gets Visual Studio's CRLF
}

bool badEncoding(const std::string& x) {
  if (x.size() >= 2 && ((x[0] == '\xFF' && x[1] == '\xFE') || (x[0] == '\xFE' && x[1] == '\xFF'))) return true;
  return x.find('\0') != std::string::npos;
}

bool plainGroup(const El& g) { return !hasAttr(g.attrs, "Label") && !hasAttr(g.attrs, "Condition"); }

struct ItemText {
  std::string type, include, filter;  // filter: "" = no metadata
};

std::string renderItem(const ItemText& it, const std::string& eol, const std::string& ind, const std::string& unit) {
  std::string s = "<" + it.type + " Include=\"" + xmlEscape(it.include) + "\"";
  if (it.filter.empty()) return s + " />";
  return s + ">" + eol + ind + unit + "<Filter>" + xmlEscape(it.filter) + "</Filter>" + eol + ind + "</" + it.type +
         ">";
}

enum class AddResult { Added, Present, Failed };

// Adds the item to the text of a project / filters file. Layout: after the last item of the same type in the first
// plain ItemGroup that has one; else into an empty plain ItemGroup; else in a new ItemGroup after the last plain
// ItemGroup that holds items (before the targets import, or </Project>, when there is none).
AddResult addItem(const std::string& x, const ItemText& item, std::string& out, std::string* error) {
  std::vector<El> els;
  if (badEncoding(x) || !scan(x, els) || els[0].name != "Project" || els[0].selfClosing || els[0].parent != -1) {
    if (error) *error = "not a readable MSBuild project (no well-formed <Project> element)";
    return AddResult::Failed;
  }
  const int root = 0;
  const std::string eol = eolOf(x);
  std::string unit = "  ";
  for (const El& e : els)
    if (e.parent == root) {
      bool own;
      const std::string ind = lineIndent(x, e.tagStart, &own);
      if (own && !ind.empty()) unit = ind;
      break;
    }

  std::vector<int> groups;
  for (size_t i = 0; i < els.size(); ++i)
    if (els[i].parent == root && els[i].name == "ItemGroup") groups.push_back(static_cast<int>(i));
  const std::string want = normInclude(item.include);
  for (int g : groups)
    for (size_t i = static_cast<size_t>(g) + 1; i < els.size() && els[i].tagStart < els[static_cast<size_t>(g)].end;
         ++i)
      if (els[i].parent == g && els[i].name == item.type && normInclude(attrValue(els[i].attrs, "Include")) == want)
        return AddResult::Present;

  auto groupIndent = [&](const El& g) {
    bool own;
    const std::string ind = lineIndent(x, g.tagStart, &own);
    return own ? ind : std::string();
  };
  auto kids = [&](int g) {
    std::vector<int> k;
    for (size_t i = static_cast<size_t>(g) + 1; i < els.size() && els[i].tagStart < els[static_cast<size_t>(g)].end;
         ++i)
      if (els[i].parent == g) k.push_back(static_cast<int>(i));
    return k;
  };
  auto fillGroup = [&](const El& g, size_t at, size_t replaceTo, bool wholeTag) {
    const std::string gi = groupIndent(g), ci = gi + unit;
    std::string attrs = g.attrs;
    while (!attrs.empty() && std::isspace(static_cast<unsigned char>(attrs.back()))) attrs.pop_back();
    std::string text = (wholeTag ? "<" + g.name + attrs + ">" : std::string()) + eol + ci +
                       renderItem(item, eol, ci, unit) + eol + gi + (wholeTag ? "</" + g.name + ">" : std::string());
    out = x;
    out.replace(at, replaceTo - at, text);
  };

  // 1. the first plain group that holds an item of this type: after its last one
  for (int g : groups) {
    if (!plainGroup(els[static_cast<size_t>(g)])) continue;
    int last = -1;
    for (int k : kids(g))
      if (els[static_cast<size_t>(k)].name == item.type) last = k;
    if (last < 0) continue;
    const El& ref = els[static_cast<size_t>(last)];
    bool own;
    std::string ind = lineIndent(x, ref.tagStart, &own);
    if (!own) ind = groupIndent(els[static_cast<size_t>(g)]) + unit;
    out = x;
    out.insert(ref.end, eol + ind + renderItem(item, eol, ind, unit));
    return AddResult::Added;
  }
  // 2. an empty plain group
  for (int g : groups) {
    const El& ge = els[static_cast<size_t>(g)];
    if (!plainGroup(ge) || !kids(g).empty()) continue;
    // only a group that holds nothing but white space is empty: stray text or a comment is not ours to replace
    if (!ge.selfClosing && std::any_of(x.begin() + static_cast<std::ptrdiff_t>(ge.openEnd),
                                       x.begin() + static_cast<std::ptrdiff_t>(ge.closeStart),
                                       [](char c) { return !std::isspace(static_cast<unsigned char>(c)); }))
      continue;
    if (ge.selfClosing) fillGroup(ge, ge.tagStart, ge.end, true);
    else fillGroup(ge, ge.openEnd, ge.closeStart, false);
    return AddResult::Added;
  }
  // 3. a new group
  int anchor = -1;
  for (int g : groups)
    if (plainGroup(els[static_cast<size_t>(g)]) && !kids(g).empty()) anchor = g;
  if (anchor >= 0) {
    const El& a = els[static_cast<size_t>(anchor)];
    bool own;
    std::string gi = lineIndent(x, a.tagStart, &own);
    if (!own) gi = unit;
    const std::string ci = gi + unit;
    out = x;
    out.insert(a.end,
               eol + gi + "<ItemGroup>" + eol + ci + renderItem(item, eol, ci, unit) + eol + gi + "</ItemGroup>");
    return AddResult::Added;
  }
  // No item group of its own: before the C++ targets import, else before </Project>.
  size_t at = std::string::npos;
  for (const El& e : els)
    if (e.parent == root && e.name == "Import" &&
        attrValue(e.attrs, "Project").find("Microsoft.Cpp.targets") != std::string::npos) {
      at = e.tagStart;
      break;
    }
  if (at == std::string::npos) at = els[0].closeStart;
  bool own;
  const std::string here = lineIndent(x, at, &own);
  const std::string gi = own && at != els[0].closeStart ? here : unit;
  const std::string ci = gi + unit;
  const std::string group = "<ItemGroup>" + eol + ci + renderItem(item, eol, ci, unit) + eol + gi + "</ItemGroup>";
  out = x;
  if (own) out.insert(at - here.size(), gi + group + eol);
  else out.insert(at, group + eol);
  return AddResult::Added;
}

}  // namespace

Support classify(const fs::path& project, std::string* why) {
  if (hasExt(project, {".vcxproj"})) return Support::Edit;
  std::string x;
  if (readRegularFile(project, x) && !badEncoding(x)) {
    std::vector<El> els;
    if (scan(x, els) && els[0].name == "Project" && hasAttr(els[0].attrs, "Sdk")) {
      if (why) *why = "an SDK-style project includes its files by glob";
      return Support::NoEditNeeded;
    }
  }
  if (why) *why = "only .vcxproj projects are edited";
  return Support::Unsupported;
}

std::string itemTypeFor(const fs::path& file) {
  if (hasExt(file, {".cpp", ".c", ".cc", ".cxx", ".c++", ".cppm", ".ixx"})) return "ClCompile";
  if (hasExt(file, {".h", ".hpp", ".hh", ".hxx", ".h++", ".hm", ".inl", ".ipp"})) return "ClInclude";
  if (hasExt(file, {".rc"})) return "ResourceCompile";
  return "None";
}

std::string nameProblem(const std::string& fileName) {
  for (unsigned char c : fileName)
    if (c < 0x20) return "a project file name cannot contain control characters";
  if (fileName.find_first_of(";%$@*?") != std::string::npos)
    return "a project file name cannot contain ; % $ @ * ? (MSBuild special characters)";
  return {};
}

std::string includeFor(const fs::path& project, const fs::path& file) {
  const fs::path rel = file.lexically_relative(project.parent_path());
  std::string s = pathKeyUtf8(rel.empty() || rel.is_absolute() ? file : rel);
  std::replace(s.begin(), s.end(), '/', '\\');
  return s;
}

std::vector<fs::path> AddPlan::files() const {
  std::vector<fs::path> out;
  if (editProject) out.push_back(project);
  if (editFilters) out.push_back(filters);
  return out;
}

bool planAdd(const AddRequest& req, AddPlan& plan, std::string* error) {
  plan = AddPlan();
  plan.project = req.project;
  plan.filters = req.project;
  plan.filters += ".filters";
  plan.itemType = itemTypeFor(req.file);
  plan.include = includeFor(req.project, req.file);
  std::string why;
  if (classify(req.project, &why) != Support::Edit) {
    if (error) *error = why;
    return false;
  }
  if (const std::string p = nameProblem(plan.include); !p.empty()) {  // the folders count too
    if (error) *error = p;
    return false;
  }
  for (unsigned char c : req.filter)
    if (c < 0x20) {
      if (error) *error = "a filter name cannot contain control characters";
      return false;
    }
  std::string err;
  if (!readRegularFile(req.project, plan.projectOld, &err)) {
    if (error) *error = "cannot read " + pathUtf8(req.project.filename()) + (err.empty() ? "" : ": " + err);
    return false;
  }
  const ItemText item{plan.itemType, plan.include, {}};
  switch (addItem(plan.projectOld, item, plan.projectText, error)) {
    case AddResult::Failed: return false;
    case AddResult::Added: plan.editProject = true; break;
    case AddResult::Present: break;
  }
  std::error_code ec;
  if (!req.filter.empty() && fs::is_regular_file(plan.filters, ec)) {
    if (!readRegularFile(plan.filters, plan.filtersOld, &err)) {
      if (error) *error = "cannot read " + pathUtf8(plan.filters.filename()) + (err.empty() ? "" : ": " + err);
      return false;
    }
    ItemText fi = item;
    fi.filter = req.filter;
    switch (addItem(plan.filtersOld, fi, plan.filtersText, error)) {
      case AddResult::Failed: return false;
      case AddResult::Added: plan.editFilters = true; break;
      case AddResult::Present: break;
    }
  }
  return true;
}

bool isWritable(const fs::path& file) {
  std::error_code ec;
  const fs::file_status st = fs::status(file, ec);
  if (ec || !fs::is_regular_file(st)) return false;
  constexpr fs::perms w = fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
  if ((st.permissions() & w) == fs::perms::none) return false;
  std::ofstream probe(file, std::ios::binary | std::ios::app);  // appends nothing
  return static_cast<bool>(probe);
}

std::vector<fs::path> unwritable(const AddPlan& plan) {
  std::vector<fs::path> out;
  for (const fs::path& f : plan.files())
    if (!isWritable(f)) out.push_back(f);
  return out;
}

namespace {
bool writeAll(const fs::path& p, const std::string& text) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f << text;
  f.close();
  return static_cast<bool>(f);
}
}  // namespace

bool commit(const AddPlan& plan, std::string* error) {
  const std::vector<fs::path> ro = unwritable(plan);
  if (!ro.empty()) {
    if (error) *error = pathUtf8(ro[0].filename()) + " is read-only";
    return false;
  }
  if (plan.editProject && !writeAll(plan.project, plan.projectText)) {
    writeAll(plan.project, plan.projectOld);
    if (error) *error = "cannot write " + pathUtf8(plan.project.filename());
    return false;
  }
  if (plan.editFilters && !writeAll(plan.filters, plan.filtersText)) {
    writeAll(plan.filters, plan.filtersOld);
    if (plan.editProject) writeAll(plan.project, plan.projectOld);
    if (error) *error = "cannot write " + pathUtf8(plan.filters.filename());
    return false;
  }
  return true;
}

}  // namespace tf::projedit
