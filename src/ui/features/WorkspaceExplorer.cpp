#include "ui/features/WorkspaceExplorer.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "core/JsonUtil.h"
#include "core/PathUtil.h"
#include "core/Trash.h"
#include "semantic/CompileCommands.h"
#include "semantic/XmlLite.h"
#include "ui/features/VcsState.h"

namespace tf::ui {

namespace fs = std::filesystem;

namespace {

constexpr const char* kSolutionFolderType = "{2150E333-8FDC-42A3-9474-1A3956D46DE8}";
constexpr size_t kMaxGlobResults = 200000;
// What one Include pattern may spend on reading directories. A pattern like "/**/x.cpp" (or any `**` over a huge tree
// or a slow network share) would otherwise walk the whole file system and never come back.
constexpr size_t kMaxGlobDirectories = 100000;
constexpr std::chrono::milliseconds kMaxGlobTime{1500};
struct GlobBudget {
  size_t dirsLeft = kMaxGlobDirectories;
  std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kMaxGlobTime;
  bool alive() const { return dirsLeft != 0 && std::chrono::steady_clock::now() <= deadline; }
  // Called before every directory read.
  bool spend() {
    if (!alive()) return false;
    --dirsLeft;
    return true;
  }
};

std::string lower(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

std::string slashes(std::string s) {
  std::replace(s.begin(), s.end(), '\\', '/');
  return s;
}

std::vector<std::string> splitOn(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i <= s.size()) {
    size_t j = s.find(sep, i);
    if (j == std::string::npos) j = s.size();
    if (j > i) out.push_back(s.substr(i, j - i));
    i = j + 1;
  }
  return out;
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

bool looksAbsolute(const std::string& p) {
  return !p.empty() && (p[0] == '/' || (p.size() > 2 && p[1] == ':' && (p[2] == '/' || p[2] == '\\')));
}

// MSBuild-style wildcard: '*' any run, '?' one character, case-insensitive.
bool wildMatch(const std::string& pat, size_t pi, const std::string& s, size_t si) {
  while (pi < pat.size()) {
    if (pat[pi] == '*') {
      while (pi < pat.size() && pat[pi] == '*') ++pi;
      if (pi == pat.size()) return true;
      for (size_t k = si; k <= s.size(); ++k)
        if (wildMatch(pat, pi, s, k)) return true;
      return false;
    }
    if (si >= s.size()) return false;
    if (pat[pi] != '?' &&
        std::tolower(static_cast<unsigned char>(pat[pi])) != std::tolower(static_cast<unsigned char>(s[si])))
      return false;
    ++pi;
    ++si;
  }
  return si == s.size();
}

// Expands `segs` (a '/'-split pattern, "**" = any number of directories) below `base`.
void globExpand(const fs::path& base, const std::vector<std::string>& segs, size_t i, std::vector<std::string>& out,
                GlobBudget& budget) {
  if (i >= segs.size() || out.size() >= kMaxGlobResults) return;
  const std::string& seg = segs[i];
  const bool last = i + 1 == segs.size();
  std::error_code ec;
  if (seg == "**") {
    globExpand(base, segs, i + 1, out, budget);  // zero directories
    if (!budget.spend()) return;
    for (fs::directory_iterator it(base, ec), end; !ec && it != end && budget.alive(); it.increment(ec))
      if (it->is_directory(ec)) globExpand(it->path(), segs, i, out, budget);
    return;
  }
  const bool wild = seg.find_first_of("*?") != std::string::npos;
  if (!wild) {
    const fs::path next = base / seg;
    if (last) {
      if (fs::is_regular_file(next, ec)) out.push_back(next.generic_string());
    } else globExpand(next, segs, i + 1, out, budget);
    return;
  }
  if (!budget.spend()) return;
  for (fs::directory_iterator it(base, ec), end; !ec && it != end && budget.alive(); it.increment(ec)) {
    const std::string name = it->path().filename().string();
    if (!wildMatch(seg, 0, name, 0)) continue;
    if (last) {
      if (it->is_regular_file(ec)) out.push_back(it->path().generic_string());
    } else if (it->is_directory(ec)) globExpand(it->path(), segs, i + 1, out, budget);
  }
}

// An Include value to absolute paths: macros of the project / solution directory substituted, wildcards expanded;
// anything else with $(...) in it is skipped (it needs the MSBuild property evaluation tenfold does not do here).
std::vector<std::string> resolveInclude(const fs::path& projDir, const fs::path& slnDir, std::string inc) {
  auto subst = [&](const std::string& macro, const std::string& value) {
    for (size_t at; (at = inc.find(macro)) != std::string::npos;) inc.replace(at, macro.size(), value);
  };
  const std::string pd = projDir.generic_string() + "/";
  subst("$(ProjectDir)", pd);
  subst("$(MSBuildProjectDirectory)", projDir.generic_string());
  if (!slnDir.empty()) subst("$(SolutionDir)", slnDir.generic_string() + "/");
  if (inc.find("$(") != std::string::npos) return {};
  inc = slashes(inc);
  std::vector<std::string> out;
  if (inc.find_first_of("*?") == std::string::npos) {
    const fs::path p = looksAbsolute(inc) ? fs::path(inc) : projDir / inc;
    out.push_back(p.lexically_normal().generic_string());
    return out;
  }
  fs::path base = looksAbsolute(inc) ? fs::path() : projDir;
  std::vector<std::string> segs = splitOn(inc, '/');
  if (looksAbsolute(inc) && !segs.empty()) {
    base = inc[0] == '/' ? fs::path("/") : fs::path(segs.front() + "/");
    if (inc[0] != '/') segs.erase(segs.begin());
  }
  std::vector<std::string> found;
  GlobBudget budget;
  globExpand(base, segs, 0, found, budget);
  std::sort(found.begin(), found.end());
  for (const std::string& f : found) out.push_back(fs::path(f).lexically_normal().generic_string());
  return out;
}

bool isNonFileItem(const std::string& t) {
  static const std::set<std::string> skip = {"ProjectConfiguration",
                                             "ProjectReference",
                                             "Reference",
                                             "PackageReference",
                                             "COMReference",
                                             "COMFileReference",
                                             "Import",
                                             "Filter",
                                             "Service",
                                             "Folder",
                                             "BootstrapperPackage",
                                             "WebReferenceUrl",
                                             "AdditionalIncludeDirectories"};
  return skip.count(t) != 0;
}

// Folder nodes below `base` for the segments, created once per (base, segment) in `cache`.
// `diskBase` (a generic path, or empty for virtual folders such as filters): the folders created get the path
// diskBase/seg/... so file operations know them.
int ensureFolders(TreeModel& m, std::unordered_map<std::string, int>& cache, int base,
                  const std::vector<std::string>& segs, const std::string& diskBase = {}) {
  int cur = base;
  std::string key = std::to_string(base);
  std::string disk = diskBase;
  for (const std::string& seg : segs) {
    key += '\x1f';
    key += lower(seg);
    if (!disk.empty()) disk += "/" + seg;
    auto it = cache.find(key);
    if (it == cache.end()) it = cache.emplace(key, m.add(cur, seg, TreeKind::Folder, disk)).first;
    cur = it->second;
  }
  return cur;
}

struct SolutionExtras {
  std::map<std::string, std::string> nestedIn;              // project guid -> parent folder guid (upper case)
  std::map<std::string, std::vector<std::string>> itemsOf;  // solution folder guid -> relative file paths
};

SolutionExtras readSolutionExtras(const fs::path& sln) {
  SolutionExtras x;
  std::ifstream in(sln);
  std::string line, curGuid;
  bool inNested = false, inItems = false;
  auto up = [](std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
  };
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.rfind("Project(", 0) == 0) {
      const size_t last = line.rfind('"');
      const size_t prev = last == std::string::npos || last == 0 ? std::string::npos : line.rfind('"', last - 1);
      curGuid = prev == std::string::npos ? std::string() : up(line.substr(prev + 1, last - prev - 1));
    } else if (line.rfind("EndProject", 0) == 0) {
      curGuid.clear();
    } else if (line.rfind("ProjectSection(SolutionItems)", 0) == 0) {
      inItems = true;
    } else if (line.rfind("EndProjectSection", 0) == 0) {
      inItems = false;
    } else if (line.rfind("GlobalSection(NestedProjects)", 0) == 0) {
      inNested = true;
    } else if (line.rfind("EndGlobalSection", 0) == 0) {
      inNested = false;
    } else if (inItems || inNested) {
      const size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      const std::string lhs = trim(line.substr(0, eq)), rhs = trim(line.substr(eq + 1));
      if (inNested) x.nestedIn[up(lhs)] = up(rhs);
      else if (!curGuid.empty()) x.itemsOf[curGuid].push_back(lhs);
    }
  }
  return x;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Project files
// ---------------------------------------------------------------------------------------------------------------

ProjectContents WorkspaceExplorer::readProject(const fs::path& project) {
  ProjectContents out;
  std::error_code ec;
  if (!fs::is_regular_file(project, ec)) return out;
  std::string err;
  const std::unique_ptr<xml::Node> doc = xml::parseFile(project.string(), &err);
  if (!doc) return out;
  out.loaded = true;
  const fs::path dir = project.parent_path();
  std::set<std::string> seen;
  std::map<std::string, std::string> filterOf;  // lower-case "type|include" -> filter, from the filters file

  fs::path filtersPath = project;
  filtersPath += ".filters";
  if (fs::is_regular_file(filtersPath, ec)) {
    if (const std::unique_ptr<xml::Node> fdoc = xml::parseFile(filtersPath.string(), &err)) {
      out.hasFiltersFile = true;
      for (const xml::Node* group : fdoc->childrenNamed("ItemGroup"))
        for (const auto& item : group->children) {
          const std::string inc = item->attr("Include");
          if (inc.empty()) continue;
          if (item->name == "Filter") {
            out.filters.push_back(slashes(inc));
            std::replace(out.filters.back().begin(), out.filters.back().end(), '/', '\\');
            continue;
          }
          if (const xml::Node* f = item->child("Filter")) filterOf[lower(slashes(inc))] = trim(f->text);
        }
    }
  }
  for (std::string& f : out.filters) f = trim(f);

  for (const xml::Node* group : doc->childrenNamed("ItemGroup"))
    for (const auto& item : group->children) {
      if (isNonFileItem(item->name)) continue;
      const std::string inc = item->attr("Include");
      if (inc.empty()) continue;
      std::string filter;
      if (const auto it = filterOf.find(lower(slashes(inc))); it != filterOf.end()) filter = it->second;
      else if (const xml::Node* f = item->child("Filter"))
        filter = trim(f->text);  // rare: filter stored in the project
      std::vector<std::string> excluded;
      for (const xml::Node* ex : item->childrenNamed("ExcludedFromBuild")) {
        if (lower(trim(ex->text)) != "true") continue;
        const std::string cond = ex->attr("Condition");
        std::string config = "*";
        if (const size_t eq = cond.find("==");
            eq != std::string::npos) {  // '$(Configuration)|$(Platform)'=='Debug|x64'
          const size_t a = cond.find('\'', eq), b = a == std::string::npos ? a : cond.find('\'', a + 1);
          if (b != std::string::npos) config = cond.substr(a + 1, b - a - 1);
        }
        excluded.push_back(config);
      }
      for (const std::string& p : resolveInclude(dir, {}, inc))
        if (seen.insert(p).second) out.files.push_back({p, filter, excluded});
    }
  return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Trees
// ---------------------------------------------------------------------------------------------------------------

namespace {
// Adds the files of one project under `projNode`; `cache` is shared with addUnlistedFiles.
void addProjectFiles(TreeModel& m, int projNode, const ProjectContents& pc, const fs::path& projDir,
                     std::unordered_map<std::string, int>& cache) {
  const std::string dirStr = pathKeyUtf8(projDir);
  if (pc.hasFiltersFile) {
    for (const std::string& f : pc.filters) ensureFolders(m, cache, projNode, splitOn(f, '\\'));
    for (const ProjectFile& f : pc.files) {
      const int parent =
          f.filter.empty() ? projNode : ensureFolders(m, cache, projNode, splitOn(slashes(f.filter), '/'));
      m.add(parent, pathKeyUtf8(pathFromUtf8(f.path).filename()), TreeKind::File, f.path);
    }
    return;
  }
  for (const ProjectFile& f : pc.files) {  // no filters: group by the folders on disk, relative to the project
    std::vector<std::string> segs;
    std::string rel;
    if (f.path.size() > dirStr.size() + 1 && f.path.compare(0, dirStr.size(), dirStr) == 0 &&
        f.path[dirStr.size()] == '/')
      rel = f.path.substr(dirStr.size() + 1);
    else rel = pathKeyUtf8(pathFromUtf8(f.path).lexically_relative(projDir));
    if (rel.empty()) rel = pathKeyUtf8(pathFromUtf8(f.path).filename());
    segs = splitOn(rel, '/');
    bool outside = false;
    while (!segs.empty() && (segs.front() == ".." || segs.front() == ".")) {
      outside = outside || segs.front() == "..";
      segs.erase(segs.begin());
    }
    if (segs.empty()) segs.push_back(pathKeyUtf8(pathFromUtf8(f.path).filename()));
    const std::string name = segs.back();
    segs.pop_back();
    // The folders are the project's own directories only when the path stayed below the project folder.
    m.add(ensureFolders(m, cache, projNode, segs, outside ? std::string() : dirStr), name, TreeKind::File, f.path);
  }
}

// Show All Files: the workspace files below the project folder that the project does not list, dimmed, and the empty
// folders made from the tree.
void addUnlistedFiles(TreeModel& m, int projNode, const ProjectContents& pc, const fs::path& projDir,
                      const std::vector<fs::path>& all, const std::vector<fs::path>& extraDirs,
                      std::unordered_map<std::string, int>& cache) {
  const std::string dirStr = pathKeyUtf8(projDir);
  std::unordered_set<std::string> listed;
  for (const ProjectFile& f : pc.files) listed.insert(pathKey(f.path, nativePathStyle()));
  auto under = [&](const std::string& s) {
    return s.size() > dirStr.size() + 1 && s.compare(0, dirStr.size(), dirStr) == 0 && s[dirStr.size()] == '/';
  };
  for (const fs::path& p : all) {
    const std::string s = pathKeyUtf8(p);
    if (!under(s) || listed.count(pathKey(s, nativePathStyle()))) continue;
    std::vector<std::string> segs = splitOn(s.substr(dirStr.size() + 1), '/');
    const std::string name = segs.back();
    segs.pop_back();
    const int parent = ensureFolders(m, cache, projNode, segs, dirStr);
    m.node(m.add(parent, name, TreeKind::File, s)).dim = true;
  }
  for (const fs::path& d : extraDirs) {
    const std::string s = pathKeyUtf8(d);
    if (under(s)) ensureFolders(m, cache, projNode, splitOn(s.substr(dirStr.size() + 1), '/'), dirStr);
  }
}
}  // namespace

TreeModel WorkspaceExplorer::buildFolderTree(const fs::path& root, const std::vector<fs::path>& files,
                                             const std::vector<fs::path>& extraDirs) {
  TreeModel m;
  const std::string rootStr = pathKeyUtf8(root);
  std::string rootName = pathKeyUtf8(root.filename());
  if (rootName.empty()) rootName = rootStr;
  m.add(-1, rootName, TreeKind::Root, rootStr);
  std::unordered_map<std::string, int> dirs;
  // The folder node for a directory relative to the root ("a/b"), created top-down; the folders carry their path.
  auto ensureDir = [&](const std::string& relDir) {
    if (relDir.empty()) return 0;
    if (const auto it = dirs.find(relDir); it != dirs.end()) return it->second;
    size_t pos = 0;
    int cur = 0;
    while (pos < relDir.size()) {
      size_t end = relDir.find('/', pos);
      if (end == std::string::npos) end = relDir.size();
      const std::string prefix = relDir.substr(0, end);
      auto jt = dirs.find(prefix);
      if (jt == dirs.end())
        jt = dirs.emplace(prefix, m.add(cur, relDir.substr(pos, end - pos), TreeKind::Folder, rootStr + "/" + prefix))
                 .first;
      cur = jt->second;
      pos = end + 1;
    }
    return cur;
  };
  std::string lastDir;
  int lastNode = 0;
  bool haveLast = false;
  for (const fs::path& p : files) {
    const std::string s = pathKeyUtf8(p);
    if (s.size() <= rootStr.size() + 1 || s.compare(0, rootStr.size(), rootStr) != 0 || s[rootStr.size()] != '/')
      continue;
    const size_t slash = s.rfind('/');
    const std::string relDir = slash > rootStr.size() ? s.substr(rootStr.size() + 1, slash - rootStr.size() - 1) : "";
    int parent;
    if (haveLast && relDir == lastDir) parent = lastNode;
    else {
      parent = ensureDir(relDir);
      lastDir = relDir;
      lastNode = parent;
      haveLast = true;
    }
    m.add(parent, s.substr(slash + 1), TreeKind::File, s);
  }
  for (const fs::path& d : extraDirs) {
    const std::string s = pathKeyUtf8(d);
    if (s.size() > rootStr.size() + 1 && s.compare(0, rootStr.size(), rootStr) == 0 && s[rootStr.size()] == '/')
      ensureDir(s.substr(rootStr.size() + 1));
  }
  m.sortAll();
  m.indexPaths();
  return m;
}

TreeModel WorkspaceExplorer::buildFolderTree(const Workspace& ws) {
  std::vector<fs::path> all;
  all.reserve(ws.fileCount());
  for (const auto* list : {&ws.sourceFiles(), &ws.headerFiles(), &ws.otherFiles()})
    all.insert(all.end(), list->begin(), list->end());
  return buildFolderTree(ws.root(), all);
}

TreeModel WorkspaceExplorer::buildSolutionTree(const fs::path& sln, const std::vector<fs::path>* unlisted,
                                               const std::vector<fs::path>& extraDirs) {
  TreeModel m;
  const compdb::SolutionInfo info = compdb::readSolution(sln);
  const SolutionExtras extras = readSolutionExtras(sln);
  const int root = m.add(-1, "", TreeKind::Solution, pathKeyUtf8(sln));
  std::map<std::string, const compdb::SolutionProject*> byGuid;
  size_t projects = 0;
  for (const compdb::SolutionProject& p : info.projects) {
    byGuid[p.guid] = &p;
    if (lower(p.typeGuid) != lower(kSolutionFolderType)) ++projects;
  }
  std::map<std::string, int> made;
  std::set<std::string> visiting;
  const fs::path slnDir = sln.parent_path();
  std::function<int(const std::string&)> make = [&](const std::string& guid) -> int {
    if (const auto it = made.find(guid); it != made.end()) return it->second;
    const compdb::SolutionProject& p = *byGuid.at(guid);
    int parent = root;
    if (const auto n = extras.nestedIn.find(guid);
        n != extras.nestedIn.end() && byGuid.count(n->second) && visiting.insert(guid).second)
      parent = make(n->second);
    if (const auto it = made.find(guid); it != made.end()) return it->second;  // a cycle made it meanwhile
    int id;
    if (lower(p.typeGuid) == lower(kSolutionFolderType)) {
      id = m.add(parent, p.name, TreeKind::Folder);
      if (const auto it = extras.itemsOf.find(guid); it != extras.itemsOf.end())
        for (const std::string& rel : it->second) {
          const std::string full = pathKeyUtf8((slnDir / pathFromUtf8(slashes(rel))).lexically_normal());
          m.add(id, pathKeyUtf8(pathFromUtf8(full).filename()), TreeKind::File, full);
        }
    } else {
      id = m.add(parent, p.name, TreeKind::Project, pathKeyUtf8(p.path));
      const ProjectContents pc = readProject(p.path);
      std::unordered_map<std::string, int> cache;
      addProjectFiles(m, id, pc, p.path.parent_path(), cache);
      if (unlisted) addUnlistedFiles(m, id, pc, p.path.parent_path(), *unlisted, extraDirs, cache);
    }
    made[guid] = id;
    return id;
  };
  for (const compdb::SolutionProject& p : info.projects) make(p.guid);
  m.node(root).name = "Solution '" + pathUtf8(sln.stem()) + "' (" + std::to_string(projects) +
                      (projects == 1 ? " project)" : " projects)");
  m.sortAll();
  m.indexPaths();
  return m;
}

TreeModel WorkspaceExplorer::buildTree(const Workspace& ws) { return buildTree(ws, TreeOptions{}); }

TreeModel WorkspaceExplorer::buildTree(const Workspace& ws, const TreeOptions& opts) {
  if (!opts.folderView) {
    const std::vector<fs::path> slns = ws.solutionFiles();
    if (!slns.empty()) {
      std::vector<fs::path> all;
      if (opts.showAll) {
        all.reserve(ws.fileCount());
        for (const auto* list : {&ws.sourceFiles(), &ws.headerFiles(), &ws.otherFiles()})
          all.insert(all.end(), list->begin(), list->end());
      }
      TreeModel m = buildSolutionTree(slns.front(), opts.showAll ? &all : nullptr, opts.extraDirs);
      if (m.node(0).expandable()) return m;
    }
  }
  std::vector<fs::path> all;
  all.reserve(ws.fileCount());
  for (const auto* list : {&ws.sourceFiles(), &ws.headerFiles(), &ws.otherFiles()})
    all.insert(all.end(), list->begin(), list->end());
  return buildFolderTree(ws.root(), all, opts.extraDirs);
}

// ---------------------------------------------------------------------------------------------------------------
// The panel
// ---------------------------------------------------------------------------------------------------------------

WorkspaceExplorer::WorkspaceExplorer(Editor& editor, Workbench& bench, Hooks hooks)
    : editor_(editor), bench_(bench), hooks_(std::move(hooks)) {
  auto tv = std::make_unique<TreeView>();
  view_ = tv.get();
  view_->markOf = [this](const TreeNode& n) {
    if (n.path.empty()) return TreeView::Mark::None;
    const fs::path p = pathFromUtf8(n.path);
    editor_.vcs().probe(p);  // Perforce: ask for the state of a file that is shown but not open (VC3)
    const TreeView::Mark m = vcsMark(editor_.vcs(), p);
    // Every synced depot file is read-only on disk; P4VS gives it no badge, so the explorer does not either.
    return m == TreeView::Mark::ReadOnly ? TreeView::Mark::None : m;
  };
  view_->onActivate = [this](int node) { opened(node); };
  view_->onClicked = [this](int node) { previewed(node); };
  view_->onClose = [this] {
    if (hooks_.focusEditor) hooks_.focusEditor();
  };
  view_->onExpansionChanged = [this] { markState(); };
  view_->onSelected = [this](int) { markState(); };
  view_->onKeyDown = [this](const InputEvent& e, int node) {
    if (node < 0 || (e.mods & (ModCtrl | ModAlt | ModSuper | ModShift))) return false;
    if (e.key == Key::F2) return rename(node), true;  // Visual Studio: F2 renames (the tree has the keys)
    if (e.key == Key::Delete) return remove(node), true;
    return false;
  };
  view_->canDrop = [this](int src, int dst) { return canDrop(src, dst); };
  view_->onDrop = [this](int src, int dst, bool copy) { move(src, dst, copy); };
  if (hooks_.env) prompt_ = std::make_unique<ExplorerPrompt>(*hooks_.env);
  bench_.addSidePanel(kPanel, std::move(tv));
  if (visible()) rebuild();  // restored open from layout.json
}

WorkspaceExplorer::~WorkspaceExplorer() {
  if (stateDirty_) saveState();
  prompt_.reset();
  if (view_) {
    view_->markOf = nullptr;
    view_->onActivate = nullptr;
    view_->onClicked = nullptr;
    view_->onClose = nullptr;
    view_->onExpansionChanged = nullptr;
    view_->onSelected = nullptr;
    view_->onKeyDown = nullptr;
    view_->canDrop = nullptr;
    view_->onDrop = nullptr;
  }
}

bool WorkspaceExplorer::hasSolution() const {
  for (const fs::path& sln : editor_.workspace().solutionFiles())
    if (!compdb::readSolution(sln).projects.empty()) return true;
  return false;
}

Position WorkspaceExplorer::caretFor(const fs::path& file) const {
  if (Document* cur = editor_.currentDocument(); cur && pathsEqual(cur->path(), file)) return editor_.cursor().pos;
  return bench_.lastCaretOf(file);
}

void WorkspaceExplorer::opened(int node) {
  const TreeNode& n = view_->model().node(node);
  if (n.path.empty()) return;
  const fs::path file = pathFromUtf8(n.path);
  std::string err;
  if (!editor_.gotoLocation({file, caretFor(file)}, &err)) {
    if (hooks_.message) hooks_.message("Cannot open " + pathUtf8(file.filename()) + (err.empty() ? "" : ": " + err));
    return;
  }
  bench_.sync();
  bench_.promoteFile(file);  // Open / Enter / double click keeps a previewed file open
  if (hooks_.focusEditor) hooks_.focusEditor();
}

// A single click on a file (VS-TABS T2): it opens in the preview tab and the keys stay in the tree.
void WorkspaceExplorer::previewed(int node) {
  if (!bench_.previewEnabled()) return;
  const TreeNode& n = view_->model().node(node);
  if (n.kind != TreeKind::File || n.path.empty()) return;
  const fs::path file = pathFromUtf8(n.path);
  const bool hadFocus = view_->hasFocus();
  std::string err;
  bench_.withPreview(true, [&] { editor_.gotoLocation({file, caretFor(file)}, &err); });
  if (!err.empty() && hooks_.message) hooks_.message("Cannot open " + pathUtf8(file.filename()) + ": " + err);
  if (hadFocus) view_->setFocus();
}

uint64_t WorkspaceExplorer::inputStamp() const {
  uint64_t h = 1469598103934665603ull;
  std::error_code ec;
  for (const fs::path& p : inputs_) {
    const auto t = fs::last_write_time(p, ec);
    const uint64_t v = ec ? 0xdead : static_cast<uint64_t>(t.time_since_epoch().count());
    const uint64_t sz = ec ? 0 : static_cast<uint64_t>(fs::file_size(p, ec));
    h = (h ^ v) * 1099511628211ull;
    h = (h ^ sz) * 1099511628211ull;
  }
  return h;
}

void WorkspaceExplorer::rebuild() {
  const Workspace& ws = editor_.workspace();
  const bool restoring = !stateLoaded_;
  if (restoring) loadState();
  extraDirs_.erase(std::remove_if(extraDirs_.begin(), extraDirs_.end(),
                                  [](const fs::path& d) {
                                    std::error_code ec;
                                    return !fs::is_directory(d, ec);
                                  }),
                   extraDirs_.end());
  TreeOptions opts;
  opts.folderView = folderView_;
  opts.showAll = showAll_;
  opts.extraDirs = extraDirs_;
  TreeModel m = buildTree(ws, opts);
  solutionMode_ = m.node(0).kind == TreeKind::Solution;
  // Inputs to watch: the solution, its projects and their filters files.
  inputs_.clear();
  if (solutionMode_) {
    const fs::path sln = pathFromUtf8(m.node(0).path);
    inputs_.push_back(sln);
    for (int k : m.node(0).kids) {
      std::vector<int> stack{k};
      while (!stack.empty()) {
        const int id = stack.back();
        stack.pop_back();
        const TreeNode& nd = m.node(id);
        if (nd.kind == TreeKind::Project && !nd.path.empty()) {
          inputs_.push_back(pathFromUtf8(nd.path));
          inputs_.push_back(pathFromUtf8(nd.path + ".filters"));
        } else if (nd.kind == TreeKind::Folder) {
          for (int c : nd.kids) stack.push_back(c);
        }
      }
    }
  }
  view_->setModel(std::move(m));
  if (restoring && havePending_) {  // the first build of this session: the saved expansion and selection
    view_->setExpandedKeys(pendingKeys_);
    view_->selectKey(pendingSel_);
    havePending_ = false;
  }
  built_ = true;
  revision_ = ws.revision();
  stamp_ = inputStamp();
  vcsGen_ = editor_.vcs().generation();
  pending_ = false;
}

void WorkspaceExplorer::show(bool focus) {
  bench_.showSidePanel(kPanel);
  if (bench_.bounds().w > 0) bench_.layout(bench_.bounds());  // the tree needs its size to scroll a row into view
  if (!built_) rebuild();
  if (focus) view_->setFocus();
}

void WorkspaceExplorer::hide() {
  const bool hadFocus = view_->hasFocus();
  bench_.hideSidePanel();
  if (hadFocus && hooks_.focusEditor) hooks_.focusEditor();
}

void WorkspaceExplorer::toggle() {
  if (visible()) hide();
  else show(true);
}

bool WorkspaceExplorer::reveal(const fs::path& file) {
  const int node = view_->model().findPath(pathKeyUtf8(file));
  if (node < 0) return false;
  return view_->reveal(node);
}

bool WorkspaceExplorer::showCurrentFile(bool toggle) {
  Document* cur = editor_.currentDocument();
  if (!cur || cur->isUntitled()) {
    if (hooks_.message) hooks_.message("No file to show in the workspace tree");
    if (!visible()) show(true);
    return false;
  }
  if (visible() && built_) {
    const int node = view_->model().findPath(pathKeyUtf8(cur->path()));
    if (toggle && node >= 0 && view_->shown(node)) {
      hide();
      return true;
    }
  }
  show(true);
  if (!reveal(cur->path())) {
    if (hooks_.message) hooks_.message(pathUtf8(cur->path().filename()) + " is not in the workspace tree");
    return false;
  }
  return true;
}

int WorkspaceExplorer::tick(int64_t nowMs) {
  lastTick_ = nowMs;
  if (stateDirty_) {
    if (nowMs - stateSince_ >= kStateSaveMs) saveState();
  }
  if (!visible()) {
    if (built_) built_ = false;  // closed: the next show builds afresh, nothing is polled meanwhile
    return stateDirty_ ? std::max<int>(1, static_cast<int>(kStateSaveMs - (nowMs - stateSince_))) : -1;
  }
  if (!built_) rebuild();
  int wait = -1;
  auto soonest = [&](int ms) { wait = wait < 0 ? ms : std::min(wait, ms); };
  if (stateDirty_) soonest(std::max<int>(1, static_cast<int>(kStateSaveMs - (nowMs - stateSince_))));
  const Workspace& ws = editor_.workspace();
  if (nowMs - lastPoll_ >= kPollMs) {
    lastPoll_ = nowMs;
    if (!pending_ && (ws.revision() != revision_ || inputStamp() != stamp_)) {
      pending_ = true;
      pendingSince_ = nowMs;
    }
  } else if (nowMs >= lastPoll_) {
    soonest(static_cast<int>(kPollMs - (nowMs - lastPoll_)));  // one more look after this burst of activity
  }
  if (pending_) {
    const int64_t due = pendingSince_ + kDebounceMs;
    if (nowMs >= due) {
      rebuild();
      lastPoll_ = nowMs;
    } else {
      soonest(static_cast<int>(due - nowMs));
    }
  }
  if (const uint64_t g = editor_.vcs().generation(); g != vcsGen_) {  // colours / markers follow version control
    vcsGen_ = g;
    view_->markDirty();
  }
  if (track_) {
    Document* cur = editor_.currentDocument();
    const std::string path = cur && !cur->isUntitled() ? pathKeyUtf8(cur->path()) : std::string();
    if (path != trackedFile_) {
      trackedFile_ = path;
      if (!path.empty()) reveal(cur->path());
    }
  }
  return wait;
}

// ---------------------------------------------------------------------------------------------------------------
// VS-TREE: saved state, view switches
// ---------------------------------------------------------------------------------------------------------------

fs::path WorkspaceExplorer::stateFile(const Workspace& ws) { return ws.cacheDir() / "explorer.json"; }

void WorkspaceExplorer::markState() {
  if (!stateDirty_) stateSince_ = lastTick_;
  stateDirty_ = true;
}

void WorkspaceExplorer::loadState() {
  stateLoaded_ = true;
  std::string text;
  if (!readRegularFile(stateFile(editor_.workspace()), text)) return;
  const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
  if (!j.is_object()) return;
  if (j.contains("view") && j["view"].is_string()) folderView_ = j["view"].get<std::string>() == "folder";
  if (j.contains("showAll") && j["showAll"].is_boolean()) showAll_ = j["showAll"].get<bool>();
  if (j.contains("expanded") && j["expanded"].is_array()) {
    pendingKeys_.clear();
    for (const auto& k : j["expanded"])
      if (k.is_string()) pendingKeys_.push_back(k.get<std::string>());
    havePending_ = true;
  }
  if (j.contains("selected") && j["selected"].is_string()) pendingSel_ = j["selected"].get<std::string>();
}

bool WorkspaceExplorer::saveState() {
  stateDirty_ = false;
  if (!built_ && !stateLoaded_) return true;  // nothing was ever shown: keep what is on disk
  nlohmann::json j;
  j["version"] = 1;
  j["view"] = folderView_ ? "folder" : "solution";
  j["showAll"] = showAll_;
  if (built_ && !view_->model().empty()) {
    j["expanded"] = view_->expandedKeys();
    j["selected"] = view_->selectedKey();
  } else if (havePending_) {
    j["expanded"] = pendingKeys_;
    j["selected"] = pendingSel_;
  } else {
    return true;  // a closed panel that was never built keeps the file
  }
  const fs::path file = stateFile(editor_.workspace());
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << dumpSafe(j, 1) << "\n";
  return static_cast<bool>(out);
}

void WorkspaceExplorer::setFolderView(bool on) {
  if (on && !hasSolution()) {
    say("No solution: the explorer already shows the folder tree");
    return;
  }
  if (!stateLoaded_) loadState();
  if (folderView_ == on) return;
  folderView_ = on;
  markState();
  if (built_) rebuild();
  say(on ? "Workspace explorer: Folder view" : "Workspace explorer: Solution view");
}

void WorkspaceExplorer::setShowAllFiles(bool on) {
  if (!stateLoaded_) loadState();
  if (showAll_ == on) return;
  showAll_ = on;
  markState();
  if (built_) rebuild();
  say(on ? (folderView_ || !hasSolution() ? "Show All Files on (it applies to the Solution view)"
                                          : "Show All Files: files not in a project are dimmed")
         : "Show All Files off");
}

bool WorkspaceExplorer::openByDefault() {
  if (!openWithSolution_ || visible() || !hasSolution()) return false;
  show(false);
  return visible();
}

// ---------------------------------------------------------------------------------------------------------------
// VS-TREE: file operations
// ---------------------------------------------------------------------------------------------------------------

namespace {
bool under(const std::string& path, const std::string& dir) {
  return path.size() > dir.size() && path.compare(0, dir.size(), dir) == 0 && path[dir.size()] == '/';
}

std::string humanSize(uintmax_t n) {
  std::string digits = std::to_string(n), out;
  for (size_t i = 0; i < digits.size(); ++i) {
    if (i && (digits.size() - i) % 3 == 0) out += ',';
    out += digits[i];
  }
  std::string s = out + (n == 1 ? " byte" : " bytes");
  if (n >= 1024) {
    char buf[32];
    const double v = n >= 1024.0 * 1024 ? n / (1024.0 * 1024) : n / 1024.0;
    std::snprintf(buf, sizeof buf, " (%.1f %s)", v, n >= 1024.0 * 1024 ? "MB" : "KB");
    s += buf;
  }
  return s;
}

// ---- the Include attributes of .vcxproj / .vcxproj.filters ----

struct ItemSpan {
  size_t tagStart, valueStart, valueEnd, elemEnd;  // the element `<Tag ... Include="value" ...>...</Tag>`
  std::string tag;
};

// Every `<Tag ... Include="v" ...>` of an MSBuild file with the extent of its element. Filter and Import are not items.
std::vector<ItemSpan> itemSpans(const std::string& x) {
  std::vector<ItemSpan> out;
  size_t at = 0;
  while ((at = x.find("Include=\"", at)) != std::string::npos) {
    const size_t vs = at + 9, ve = x.find('"', vs);
    if (ve == std::string::npos) break;
    at = ve + 1;
    const size_t lt = x.rfind('<', vs);
    if (lt == std::string::npos || x.find('>', lt) < vs) continue;  // not inside a start tag
    size_t ne = lt + 1;
    while (ne < x.size() && (std::isalnum(static_cast<unsigned char>(x[ne])) || x[ne] == '_' || x[ne] == ':')) ++ne;
    ItemSpan sp;
    sp.tag = x.substr(lt + 1, ne - lt - 1);
    if (sp.tag.empty() || sp.tag == "Filter") continue;
    const size_t gt = x.find('>', ve);
    if (gt == std::string::npos) continue;
    size_t end = gt + 1;
    if (x[gt - 1] != '/') {
      const size_t close = x.find("</" + sp.tag + ">", gt);
      if (close == std::string::npos) continue;
      end = close + sp.tag.size() + 3;
    }
    sp.tagStart = lt;
    sp.valueStart = vs;
    sp.valueEnd = ve;
    sp.elemEnd = end;
    out.push_back(std::move(sp));
  }
  return out;
}

// The absolute generic path an Include value names, "" for wildcards and unresolved properties.
std::string includePath(const fs::path& projDir, const fs::path& slnDir, std::string v) {
  if (v.find_first_of("*?") != std::string::npos) return {};
  const std::vector<std::string> r = resolveInclude(projDir, slnDir, v);
  return r.size() == 1 ? r[0] : std::string();
}

bool readAll(const fs::path& p, std::string& out) { return readRegularFile(p, out); }

bool writeAll(const fs::path& p, const std::string& text) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f << text;
  return static_cast<bool>(f);
}

// Rewrites the Include values of `file` (the project or its filters file) that name `from` (or, for a folder, lie
// under it) to name the moved location. Returns how many changed; -1 when the file could not be written.
int renameIncludes(const fs::path& file, const fs::path& from, const fs::path& to, bool isDir) {
  std::string x;
  if (!readAll(file, x)) return 0;
  const fs::path projDir = file.parent_path();
  const std::string f = pathKey(pathKeyUtf8(from), nativePathStyle());
  int changed = 0;
  std::vector<ItemSpan> spans = itemSpans(x);
  for (auto it = spans.rbegin(); it != spans.rend(); ++it) {
    const std::string value = x.substr(it->valueStart, it->valueEnd - it->valueStart);
    const std::string abs = includePath(projDir, {}, value);
    if (abs.empty()) continue;
    const std::string key = pathKey(abs, nativePathStyle());
    std::string rest;
    if (key == f) rest.clear();
    else if (isDir && under(key, f)) rest = abs.substr(f.size());
    else continue;
    const fs::path target = pathFromUtf8(pathKeyUtf8(to) + rest);
    const bool wasAbs = looksAbsolute(slashes(value)) && value.find("$(") == std::string::npos;
    std::string nv = wasAbs ? pathKeyUtf8(target) : pathKeyUtf8(target.lexically_relative(projDir));
    if (value.find('\\') != std::string::npos) std::replace(nv.begin(), nv.end(), '/', '\\');
    if (nv.empty() || nv == value) continue;
    x.replace(it->valueStart, it->valueEnd - it->valueStart, nv);
    ++changed;
  }
  if (changed && !writeAll(file, x)) return -1;
  return changed;
}

// Removes the items that name `what` (or lie under it) from `file`. Returns how many; -1 on a write failure.
int removeIncludes(const fs::path& file, const fs::path& what, bool isDir) {
  std::string x;
  if (!readAll(file, x)) return 0;
  const fs::path projDir = file.parent_path();
  const std::string w = pathKey(pathKeyUtf8(what), nativePathStyle());
  int removed = 0;
  std::vector<ItemSpan> spans = itemSpans(x);
  for (auto it = spans.rbegin(); it != spans.rend(); ++it) {
    const std::string abs = includePath(projDir, {}, x.substr(it->valueStart, it->valueEnd - it->valueStart));
    if (abs.empty()) continue;
    const std::string key = pathKey(abs, nativePathStyle());
    if (!(key == w || (isDir && under(key, w)))) continue;
    size_t b = it->tagStart, e = it->elemEnd;
    while (b > 0 && (x[b - 1] == ' ' || x[b - 1] == '\t')) --b;  // the element's own line goes away entirely
    if (b > 0 && x[b - 1] == '\n') {
      if (e < x.size() && x[e] == '\r') ++e;
      if (e < x.size() && x[e] == '\n') ++e;
    } else {
      b = it->tagStart;
    }
    x.erase(b, e - b);
    ++removed;
  }
  if (removed && !writeAll(file, x)) return -1;
  return removed;
}
}  // namespace

std::vector<fs::path> WorkspaceExplorer::projectFiles() const {
  std::vector<fs::path> out;
  const TreeModel& m = view_->model();
  for (size_t i = 0; i < m.size(); ++i)
    if (m.node(static_cast<int>(i)).kind == TreeKind::Project && !m.node(static_cast<int>(i)).path.empty())
      out.push_back(pathFromUtf8(m.node(static_cast<int>(i)).path));
  if (out.empty())  // Folder view: the projects of the solution still need to follow the files
    for (const fs::path& sln : editor_.workspace().solutionFiles())
      for (const compdb::SolutionProject& p : compdb::readSolution(sln).projects) out.push_back(p.path);
  return out;
}

fs::path WorkspaceExplorer::dirFor(int node) const {
  const TreeModel& m = view_->model();
  if (node < 0 || static_cast<size_t>(node) >= m.size()) return {};
  for (int n = node; n >= 0; n = m.node(n).parent) {
    const TreeNode& nd = m.node(n);
    if (nd.path.empty()) continue;  // a filter / solution folder: ask its parent
    const fs::path p = pathFromUtf8(nd.path);
    switch (nd.kind) {
      case TreeKind::Folder:
      case TreeKind::Root: return p;
      case TreeKind::Project:
      case TreeKind::Solution:
      case TreeKind::File: return p.parent_path();
    }
  }
  return {};
}

std::string WorkspaceExplorer::nameProblem(const std::string& name, const fs::path& dir, const fs::path& self) const {
  if (name.empty()) return "type a name";
  if (name == "." || name == "..") return "not a valid name";
  for (unsigned char c : name) {
    if (c < 0x20 || c == '/' || c == '\\') return "a name cannot contain / \\ or control characters";
    if (nativePathStyle() == PathStyle::Windows &&
        std::string("<>:\"|?*").find(static_cast<char>(c)) != std::string::npos)
      return "a name cannot contain < > : \" | ? *";
  }
  const fs::path target = dir / pathFromUtf8(name);
  std::error_code ec;
  if (!self.empty() && pathsEqual(target, self)) return {};
  if (fs::exists(fs::symlink_status(target, ec))) return "already exists";
  return {};
}

bool WorkspaceExplorer::dirtyUnder(const fs::path& path, bool isDir, std::string* which) const {
  std::vector<fs::path> candidates;
  if (isDir) candidates = editor_.workspace().filesUnder(path);
  else candidates.push_back(path);
  for (const fs::path& f : candidates) {
    const Document* d = editor_.document(f);
    if (d && d->dirty()) {
      if (which) *which = pathUtf8(f.filename());
      return true;
    }
  }
  return false;
}

// Closes the tabs of the open documents at / below `path`; `reopen` receives the current document's path when it was
// one of them.
void WorkspaceExplorer::closeDocsUnder(const fs::path& path, bool isDir, bool discard, fs::path* reopen) {
  std::vector<fs::path> files;
  if (isDir) files = editor_.workspace().filesUnder(path);
  else files.push_back(path);
  const Document* cur = editor_.currentDocument();
  for (const fs::path& f : files) {
    if (!editor_.document(f)) continue;
    if (reopen && cur && pathsEqual(cur->path(), f)) *reopen = f;
    for (int id : bench_.tabsOf(f)) bench_.closeTab(id, discard);
    if (editor_.document(f)) editor_.close(f);
  }
}

void WorkspaceExplorer::afterChange(const fs::path& select) {
  rebuild();
  if (!select.empty()) reveal(select);
  view_->markDirty();
}

bool WorkspaceExplorer::newFile(int node) {
  const fs::path dir = dirFor(node);
  if (dir.empty() || !prompt_) {
    say("New File: select a folder or a file in the tree first");
    return false;
  }
  prompt_->askName(
      "New file name", "Create", "", [this, dir](const std::string& n) { return nameProblem(n, dir, {}); },
      [this, dir](const std::string& name) {
        const fs::path file = dir / pathFromUtf8(name);
        std::ofstream out(file, std::ios::binary);
        if (!out) {
          say("Cannot create " + name);
          return;
        }
        out.close();
        editor_.workspace().noteFileAdded(file);
        afterChange(file);
        if (view_->model().findPath(pathKeyUtf8(file)) < 0)
          say("Created " + name + " (it is in no project: turn on Show All Files to see it)");
        else say("Created " + name);
      });
  return true;
}

bool WorkspaceExplorer::newFolder(int node) {
  const fs::path dir = dirFor(node);
  if (dir.empty() || !prompt_) {
    say("New Folder: select a folder or a file in the tree first");
    return false;
  }
  prompt_->askName(
      "New folder name", "Create folder", "", [this, dir](const std::string& n) { return nameProblem(n, dir, {}); },
      [this, dir](const std::string& name) {
        const fs::path folder = dir / pathFromUtf8(name);
        std::error_code ec;
        if (!fs::create_directory(folder, ec)) {
          say("Cannot create " + name + (ec ? ": " + ec.message() : std::string()));
          return;
        }
        extraDirs_.push_back(folder);
        afterChange(folder);
        say("Created folder " + name);
      });
  return true;
}

// Moves / renames a file or folder on disk and in the projects that list it; the open documents follow (their tabs
// are closed and the current one reopened). The caller has checked that the target does not exist.
bool WorkspaceExplorer::relocate(const fs::path& from, const fs::path& to, bool isDir) {
  std::string which;
  if (dirtyUnder(from, isDir, &which)) {
    say("Save " + which + " first: it has unsaved changes");
    return false;
  }
  fs::path reopen;
  closeDocsUnder(from, isDir, false, &reopen);
  std::error_code ec;
  fs::rename(from, to, ec);
  if (ec) {
    say("Cannot move " + pathUtf8(from.filename()) + ": " + ec.message());
    if (!reopen.empty()) editor_.gotoLocation({reopen, Position{}}, nullptr);
    return false;
  }
  Workspace& ws = editor_.workspace();
  if (isDir) {
    for (const fs::path& f : ws.filesUnder(from)) ws.noteFileRemoved(f);
    ws.noteTreeAdded(to);
  } else {
    ws.noteFileRenamed(from, to);
  }
  std::string failed;
  for (const fs::path& proj : projectFiles()) {
    fs::path filters = proj;
    filters += ".filters";
    if (renameIncludes(proj, from, to, isDir) < 0 || renameIncludes(filters, from, to, isDir) < 0)
      failed = pathUtf8(proj.filename());
  }
  for (fs::path& d : extraDirs_)
    if (isDir && (pathsEqual(d, from) || under(pathKeyUtf8(d), pathKeyUtf8(from))))
      d = pathFromUtf8(pathKeyUtf8(to) + pathKeyUtf8(d).substr(pathKeyUtf8(from).size()));
  if (isDir) extraDirs_.push_back(to);
  if (!reopen.empty()) {
    const std::string rest = pathKeyUtf8(reopen).substr(pathKeyUtf8(from).size());
    const fs::path now = isDir ? pathFromUtf8(pathKeyUtf8(to) + rest) : to;
    editor_.gotoLocation({now, Position{}}, nullptr);
    bench_.sync();
  }
  afterChange(to);
  if (!failed.empty()) say("Moved, but " + failed + " could not be updated");
  return true;
}

bool WorkspaceExplorer::rename(int node) {
  const TreeModel& m = view_->model();
  if (node < 0 || static_cast<size_t>(node) >= m.size() || !prompt_) return false;
  const TreeNode& n = m.node(node);
  const bool isDir = n.kind == TreeKind::Folder;
  if (!(n.kind == TreeKind::File || isDir) || n.path.empty()) {
    say("Rename: only files and folders on disk can be renamed here");
    return false;
  }
  const fs::path from = pathFromUtf8(n.path), dir = from.parent_path();
  std::string which;
  if (dirtyUnder(from, isDir, &which)) {
    say("Save " + which + " first: it has unsaved changes");
    return false;
  }
  prompt_->askName(
      isDir ? "Rename folder" : "Rename file", "Rename to", n.name,
      [this, dir, from](const std::string& name) { return nameProblem(name, dir, from); },
      [this, dir, from, isDir](const std::string& name) {
        const fs::path to = dir / pathFromUtf8(name);
        if (pathUtf8(to) == pathUtf8(from)) return;
        relocate(from, to, isDir);
      });
  return true;
}

void WorkspaceExplorer::erase(const fs::path& path, bool isDir) {
  closeDocsUnder(path, isDir, true);
  Workspace& ws = editor_.workspace();
  std::vector<fs::path> gone;
  if (isDir) gone = ws.filesUnder(path);
  std::error_code ec;
  if (trashSupported(path)) {  // the Recycle Bin / Trash; it never falls back to a permanent delete after asking
    std::string why;
    if (!moveToTrash(path, &why)) {
      say("Cannot move " + pathUtf8(path.filename()) + " to the trash: " + why);
      return;
    }
  } else {
    if (isDir) fs::remove_all(path, ec);
    else fs::remove(path, ec);
    if (ec) {
      say("Cannot delete " + pathUtf8(path.filename()) + ": " + ec.message());
      return;
    }
  }
  if (isDir) {
    for (const fs::path& f : gone) ws.noteFileRemoved(f);
  } else {
    ws.noteFileRemoved(path);
  }
  for (const fs::path& proj : projectFiles()) {
    fs::path filters = proj;
    filters += ".filters";
    removeIncludes(proj, path, isDir);
    removeIncludes(filters, path, isDir);
  }
  extraDirs_.erase(std::remove_if(extraDirs_.begin(), extraDirs_.end(),
                                  [&](const fs::path& d) {
                                    return isDir && (pathsEqual(d, path) || under(pathKeyUtf8(d), pathKeyUtf8(path)));
                                  }),
                   extraDirs_.end());
  afterChange(path.parent_path());
  say("Deleted " + pathUtf8(path.filename()));
}

bool WorkspaceExplorer::remove(int node) {
  const TreeModel& m = view_->model();
  if (node < 0 || static_cast<size_t>(node) >= m.size() || !prompt_) return false;
  const TreeNode& n = m.node(node);
  const bool isDir = n.kind == TreeKind::Folder;
  if (!(n.kind == TreeKind::File || isDir) || n.path.empty()) {
    say("Delete: only files and folders on disk can be deleted here");
    return false;
  }
  const fs::path path = pathFromUtf8(n.path);
  const bool trash = trashSupported(path);
  std::vector<std::string> details;
  if (isDir)
    details.push_back(trash ? "The folder and everything in it is moved."
                            : "The folder and everything in it is deleted.");
  std::string which;
  if (dirtyUnder(path, isDir, &which)) details.push_back(which + " has unsaved changes that are lost.");
  if (!trash) details.push_back("This cannot be undone.");
  prompt_->confirm("'" + n.name +
                       (trash ? "' will be moved to the " +
                                    std::string(nativePathStyle() == PathStyle::Windows ? "Recycle Bin." : "Trash.")
                              : "' will be deleted permanently."),
                   details, "OK", [this, path, isDir] { erase(path, isDir); });
  return true;
}

bool WorkspaceExplorer::canDrop(int src, int dst) const {
  if (!dragDrop_ || !prompt_) return false;
  const TreeModel& m = view_->model();
  if (src <= 0 || dst < 0 || src == dst || static_cast<size_t>(src) >= m.size() || static_cast<size_t>(dst) >= m.size())
    return false;
  const TreeNode& s = m.node(src);
  const TreeNode& d = m.node(dst);
  if (!(s.kind == TreeKind::File || s.kind == TreeKind::Folder) || s.path.empty()) return false;
  if (!(d.kind == TreeKind::Folder || d.kind == TreeKind::Root || d.kind == TreeKind::Project) || d.path.empty())
    return false;
  const fs::path dir = dirFor(dst);
  if (dir.empty()) return false;
  const std::string sp = s.path, dp = pathKeyUtf8(dir);
  if (pathKey(dp, nativePathStyle()) == pathKey(pathKeyUtf8(pathFromUtf8(sp).parent_path()), nativePathStyle()))
    return false;  // already there
  if (s.kind == TreeKind::Folder && (pathKey(dp, nativePathStyle()) == pathKey(sp, nativePathStyle()) ||
                                     under(pathKey(dp, nativePathStyle()), pathKey(sp, nativePathStyle()))))
    return false;  // into itself
  return true;
}

bool WorkspaceExplorer::move(int src, int dst, bool copy) {
  if (!canDrop(src, dst)) return false;
  const TreeNode& s = view_->model().node(src);
  const bool isDir = s.kind == TreeKind::Folder;
  const fs::path from = pathFromUtf8(s.path), dir = dirFor(dst), to = dir / from.filename();
  const std::string name = s.name, destName = view_->model().node(dst).name;
  std::error_code ec;
  if (fs::exists(to, ec)) {
    say(name + " already exists in " + destName);
    return false;
  }
  if (copy) {
    fs::copy(from, to, isDir ? fs::copy_options::recursive : fs::copy_options::none, ec);
    if (ec) {
      say("Cannot copy " + name + ": " + ec.message());
      return false;
    }
    Workspace& ws = editor_.workspace();
    if (isDir) ws.noteTreeAdded(to);
    else ws.noteFileAdded(to);
    if (isDir) extraDirs_.push_back(to);
    afterChange(to);
    say("Copied " + name + " to " + destName + " (the copy is in no project)");
    return true;
  }
  auto run = [this, from, to, isDir] { relocate(from, to, isDir); };
  if (!promptMove_ || !prompt_) {
    run();
    return true;
  }
  prompt_->confirm(
      "Move '" + name + "' to '" + destName + "'?",
      {"The " + std::string(isDir ? "folder" : "file") + " is moved on disk and in the projects that list it."}, "OK",
      run);
  return true;
}

bool WorkspaceExplorer::properties(int node) {
  const TreeModel& m = view_->model();
  if (node < 0 || static_cast<size_t>(node) >= m.size() || !prompt_) return false;
  const TreeNode& n = m.node(node);
  std::vector<std::pair<std::string, std::string>> rows;
  const char* kind = n.kind == TreeKind::File       ? "File"
                     : n.kind == TreeKind::Folder   ? (n.path.empty() ? "Filter" : "Folder")
                     : n.kind == TreeKind::Project  ? "Project"
                     : n.kind == TreeKind::Solution ? "Solution"
                                                    : "Folder";
  rows.push_back({"Name", n.name});
  rows.push_back({"Type", kind});
  if (!n.path.empty()) rows.push_back({"Full Path", pathUtf8(pathFromUtf8(n.path))});
  const auto cfg = editor_.activeVsConfiguration();
  // The project that lists the file: the nearest Project ancestor (Solution view), else the projects that name it.
  int projNode = -1;
  for (int a = n.parent; a >= 0; a = m.node(a).parent)
    if (m.node(a).kind == TreeKind::Project) {
      projNode = a;
      break;
    }
  std::error_code ec;
  if (n.kind == TreeKind::File && !n.path.empty()) {
    const uintmax_t sz = fs::file_size(pathFromUtf8(n.path), ec);
    rows.push_back({"Size", ec ? "unknown" : humanSize(sz)});
    std::string projName = "(none)", excluded = "No";
    if (!n.dim) {
      std::vector<fs::path> projs;
      if (projNode >= 0) projs.push_back(pathFromUtf8(m.node(projNode).path));
      else projs = projectFiles();
      for (const fs::path& p : projs) {
        const ProjectContents pc = readProject(p);
        for (const ProjectFile& f : pc.files)
          if (pathsEqual(f.path, n.path)) {
            projName = pathUtf8(p.stem());
            std::string list;
            bool active = false;
            for (const std::string& e : f.excluded) {
              if (!list.empty()) list += ", ";
              list += e == "*" ? "all configurations" : e;
              if (e == "*" || lower(e) == lower(cfg.name()) || lower(e) == lower(cfg.configuration)) active = true;
            }
            if (!f.excluded.empty()) excluded = (active ? "Yes" : "No") + std::string(" (excluded in: ") + list + ")";
            break;
          }
        if (projName != "(none)") break;
      }
    }
    rows.push_back({"Project", projName});
    if (!cfg.configuration.empty()) rows.push_back({"Configuration", cfg.name()});
    rows.push_back({"Excluded From Build", excluded});
  } else if (n.kind == TreeKind::Folder || n.kind == TreeKind::Root) {
    size_t files = 0;
    std::vector<int> stack{node};
    while (!stack.empty()) {
      const int id = stack.back();
      stack.pop_back();
      for (int k : m.node(id).kids) {
        if (m.node(k).kind == TreeKind::File) ++files;
        else stack.push_back(k);
      }
    }
    rows.push_back({"Files", std::to_string(files)});
    if (projNode >= 0) rows.push_back({"Project", m.node(projNode).name});
  } else if (n.kind == TreeKind::Project || n.kind == TreeKind::Solution) {
    size_t files = 0, projects = 0;
    std::vector<int> stack{node};
    while (!stack.empty()) {
      const int id = stack.back();
      stack.pop_back();
      for (int k : m.node(id).kids) {
        if (m.node(k).kind == TreeKind::File) ++files;
        else {
          if (m.node(k).kind == TreeKind::Project) ++projects;
          stack.push_back(k);
        }
      }
    }
    if (n.kind == TreeKind::Solution) rows.push_back({"Projects", std::to_string(projects)});
    rows.push_back({"Files", std::to_string(files)});
    if (!cfg.configuration.empty()) rows.push_back({"Configuration", cfg.name()});
  }
  prompt_->info("Properties", rows);
  return true;
}

std::vector<MenuItem> WorkspaceExplorer::operationItems(int node) {
  std::vector<MenuItem> items;
  const TreeModel& m = view_->model();
  if (node < 0 || static_cast<size_t>(node) >= m.size()) return items;
  const TreeNode& n = m.node(node);
  auto add = [&](const char* label, const char* key, std::function<void()> fn) {
    MenuItem it;
    it.label = label;
    it.shortcut = key;
    it.action = std::move(fn);
    items.push_back(std::move(it));
  };
  if (!dirFor(node).empty()) {
    add("New File...", "", [this, node] { newFile(node); });
    add("New Folder...", "", [this, node] { newFolder(node); });
  }
  if ((n.kind == TreeKind::File || n.kind == TreeKind::Folder) && !n.path.empty() && node > 0) {
    add("Rename", "F2", [this, node] { rename(node); });
    add("Delete", "Delete", [this, node] { remove(node); });
  }
  return items;
}

std::vector<MenuItem> WorkspaceExplorer::viewItems(int node) {
  std::vector<MenuItem> items;
  auto add = [&](const char* label, std::function<void()> fn, bool enabled = true) {
    MenuItem it;
    it.label = label;
    it.enabled = enabled;
    it.action = std::move(fn);
    items.push_back(std::move(it));
  };
  if (hasSolution())
    add(folderView_ ? "Switch to Solution View" : "Switch to Folder View", [this] { toggleFolderView(); });
  add(showAll_ ? "Hide Files Not in a Project" : "Show All Files", [this] { toggleShowAllFiles(); });
  add("Properties", [this, node] { properties(node); });
  return items;
}

}  // namespace tf::ui
