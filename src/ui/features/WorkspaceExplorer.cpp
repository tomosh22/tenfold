#include "ui/features/WorkspaceExplorer.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

#include "core/PathUtil.h"
#include "semantic/CompileCommands.h"
#include "semantic/XmlLite.h"
#include "ui/features/VcsState.h"

namespace tf::ui {

namespace fs = std::filesystem;

namespace {

constexpr const char* kSolutionFolderType = "{2150E333-8FDC-42A3-9474-1A3956D46DE8}";
constexpr size_t kMaxGlobResults = 200000;

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
void globExpand(const fs::path& base, const std::vector<std::string>& segs, size_t i, std::vector<std::string>& out) {
  if (i >= segs.size() || out.size() >= kMaxGlobResults) return;
  const std::string& seg = segs[i];
  const bool last = i + 1 == segs.size();
  std::error_code ec;
  if (seg == "**") {
    globExpand(base, segs, i + 1, out);  // zero directories
    for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec))
      if (it->is_directory(ec)) globExpand(it->path(), segs, i, out);
    return;
  }
  const bool wild = seg.find_first_of("*?") != std::string::npos;
  if (!wild) {
    const fs::path next = base / seg;
    if (last) {
      if (fs::is_regular_file(next, ec)) out.push_back(next.generic_string());
    } else globExpand(next, segs, i + 1, out);
    return;
  }
  for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string name = it->path().filename().string();
    if (!wildMatch(seg, 0, name, 0)) continue;
    if (last) {
      if (it->is_regular_file(ec)) out.push_back(it->path().generic_string());
    } else if (it->is_directory(ec)) globExpand(it->path(), segs, i + 1, out);
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
  globExpand(base, segs, 0, found);
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
int ensureFolders(TreeModel& m, std::unordered_map<std::string, int>& cache, int base,
                  const std::vector<std::string>& segs) {
  int cur = base;
  std::string key = std::to_string(base);
  for (const std::string& seg : segs) {
    key += '\x1f';
    key += lower(seg);
    auto it = cache.find(key);
    if (it == cache.end()) it = cache.emplace(key, m.add(cur, seg, TreeKind::Folder)).first;
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
      for (const std::string& p : resolveInclude(dir, {}, inc))
        if (seen.insert(p).second) out.files.push_back({p, filter});
    }
  return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Trees
// ---------------------------------------------------------------------------------------------------------------

namespace {
// Adds the files of one project under `projNode`.
void addProjectFiles(TreeModel& m, int projNode, const ProjectContents& pc, const fs::path& projDir) {
  std::unordered_map<std::string, int> cache;
  const std::string dirStr = projDir.generic_string();
  if (pc.hasFiltersFile) {
    for (const std::string& f : pc.filters) ensureFolders(m, cache, projNode, splitOn(f, '\\'));
    for (const ProjectFile& f : pc.files) {
      const int parent =
          f.filter.empty() ? projNode : ensureFolders(m, cache, projNode, splitOn(slashes(f.filter), '/'));
      m.add(parent, fs::path(f.path).filename().generic_string(), TreeKind::File, f.path);
    }
    return;
  }
  for (const ProjectFile& f : pc.files) {  // no filters: group by the folders on disk, relative to the project
    std::vector<std::string> segs;
    std::string rel;
    if (f.path.size() > dirStr.size() + 1 && f.path.compare(0, dirStr.size(), dirStr) == 0 &&
        f.path[dirStr.size()] == '/')
      rel = f.path.substr(dirStr.size() + 1);
    else rel = fs::path(f.path).lexically_relative(projDir).generic_string();
    if (rel.empty()) rel = fs::path(f.path).filename().generic_string();
    segs = splitOn(rel, '/');
    while (!segs.empty() && (segs.front() == ".." || segs.front() == ".")) segs.erase(segs.begin());
    if (segs.empty()) segs.push_back(fs::path(f.path).filename().generic_string());
    const std::string name = segs.back();
    segs.pop_back();
    m.add(ensureFolders(m, cache, projNode, segs), name, TreeKind::File, f.path);
  }
}
}  // namespace

TreeModel WorkspaceExplorer::buildFolderTree(const fs::path& root, const std::vector<fs::path>& files) {
  TreeModel m;
  const std::string rootStr = root.generic_string();
  std::string rootName = root.filename().generic_string();
  if (rootName.empty()) rootName = rootStr;
  m.add(-1, rootName, TreeKind::Root, rootStr);
  std::unordered_map<std::string, int> dirs;
  std::string lastDir;
  int lastNode = 0;
  bool haveLast = false;
  for (const fs::path& p : files) {
    const std::string s = p.generic_string();
    if (s.size() <= rootStr.size() + 1 || s.compare(0, rootStr.size(), rootStr) != 0 || s[rootStr.size()] != '/')
      continue;
    const size_t slash = s.rfind('/');
    const std::string relDir = slash > rootStr.size() ? s.substr(rootStr.size() + 1, slash - rootStr.size() - 1) : "";
    int parent;
    if (haveLast && relDir == lastDir) parent = lastNode;
    else {
      parent = 0;
      if (!relDir.empty()) {
        auto it = dirs.find(relDir);
        if (it != dirs.end()) parent = it->second;
        else {  // create the missing chain, top-down
          size_t pos = 0;
          int cur = 0;
          while (pos < relDir.size()) {
            size_t end = relDir.find('/', pos);
            if (end == std::string::npos) end = relDir.size();
            const std::string prefix = relDir.substr(0, end);
            auto jt = dirs.find(prefix);
            if (jt == dirs.end())
              jt = dirs.emplace(prefix, m.add(cur, relDir.substr(pos, end - pos), TreeKind::Folder)).first;
            cur = jt->second;
            pos = end + 1;
          }
          parent = cur;
        }
      }
      lastDir = relDir;
      lastNode = parent;
      haveLast = true;
    }
    m.add(parent, s.substr(slash + 1), TreeKind::File, s);
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

TreeModel WorkspaceExplorer::buildSolutionTree(const fs::path& sln) {
  TreeModel m;
  const compdb::SolutionInfo info = compdb::readSolution(sln);
  const SolutionExtras extras = readSolutionExtras(sln);
  const int root = m.add(-1, "", TreeKind::Solution, sln.generic_string());
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
          const std::string full = (slnDir / slashes(rel)).lexically_normal().generic_string();
          m.add(id, fs::path(full).filename().generic_string(), TreeKind::File, full);
        }
    } else {
      id = m.add(parent, p.name, TreeKind::Project, p.path.generic_string());
      const ProjectContents pc = readProject(p.path);
      addProjectFiles(m, id, pc, p.path.parent_path());
    }
    made[guid] = id;
    return id;
  };
  for (const compdb::SolutionProject& p : info.projects) make(p.guid);
  m.node(root).name = "Solution '" + sln.stem().string() + "' (" + std::to_string(projects) +
                      (projects == 1 ? " project)" : " projects)");
  m.sortAll();
  m.indexPaths();
  return m;
}

TreeModel WorkspaceExplorer::buildTree(const Workspace& ws) {
  const std::vector<fs::path> slns = ws.solutionFiles();
  if (!slns.empty()) {
    TreeModel m = buildSolutionTree(slns.front());
    if (m.node(0).expandable()) return m;
  }
  return buildFolderTree(ws);
}

// ---------------------------------------------------------------------------------------------------------------
// The panel
// ---------------------------------------------------------------------------------------------------------------

WorkspaceExplorer::WorkspaceExplorer(Editor& editor, Workbench& bench, Hooks hooks)
    : editor_(editor), bench_(bench), hooks_(std::move(hooks)) {
  auto tv = std::make_unique<TreeView>();
  view_ = tv.get();
  view_->markOf = [this](const TreeNode& n) {
    return n.path.empty() ? TreeView::Mark::None : vcsTabMark(editor_.vcs().fileStatus(fs::path(n.path)));
  };
  view_->onActivate = [this](int node) { opened(node); };
  view_->onClose = [this] {
    if (hooks_.focusEditor) hooks_.focusEditor();
  };
  bench_.addSidePanel(kPanel, std::move(tv));
  if (visible()) rebuild();  // restored open from layout.json
}

WorkspaceExplorer::~WorkspaceExplorer() {
  if (view_) {
    view_->markOf = nullptr;
    view_->onActivate = nullptr;
    view_->onClose = nullptr;
  }
}

Position WorkspaceExplorer::caretFor(const fs::path& file) const {
  if (Document* cur = editor_.currentDocument(); cur && pathsEqual(cur->path(), file)) return editor_.cursor().pos;
  return bench_.lastCaretOf(file);
}

void WorkspaceExplorer::opened(int node) {
  const TreeNode& n = view_->model().node(node);
  if (n.path.empty()) return;
  const fs::path file(n.path);
  std::string err;
  if (!editor_.gotoLocation({file, caretFor(file)}, &err)) {
    if (hooks_.message) hooks_.message("Cannot open " + file.filename().string() + (err.empty() ? "" : ": " + err));
    return;
  }
  bench_.sync();
  if (hooks_.focusEditor) hooks_.focusEditor();
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
  TreeModel m = buildTree(ws);
  solutionMode_ = m.node(0).kind == TreeKind::Solution;
  // Inputs to watch: the solution, its projects and their filters files.
  inputs_.clear();
  if (solutionMode_) {
    const fs::path sln(m.node(0).path);
    inputs_.push_back(sln);
    for (int k : m.node(0).kids) {
      std::vector<int> stack{k};
      while (!stack.empty()) {
        const int id = stack.back();
        stack.pop_back();
        const TreeNode& nd = m.node(id);
        if (nd.kind == TreeKind::Project && !nd.path.empty()) {
          inputs_.push_back(nd.path);
          inputs_.push_back(nd.path + ".filters");
        } else if (nd.kind == TreeKind::Folder) {
          for (int c : nd.kids) stack.push_back(c);
        }
      }
    }
  }
  view_->setModel(std::move(m));
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
  const int node = view_->model().findPath(file.generic_string());
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
    const int node = view_->model().findPath(cur->path().generic_string());
    if (toggle && node >= 0 && view_->shown(node)) {
      hide();
      return true;
    }
  }
  show(true);
  if (!reveal(cur->path())) {
    if (hooks_.message) hooks_.message(cur->path().filename().string() + " is not in the workspace tree");
    return false;
  }
  return true;
}

int WorkspaceExplorer::tick(int64_t nowMs) {
  if (!visible()) {
    if (built_) built_ = false;  // closed: the next show builds afresh, nothing is polled meanwhile
    return -1;
  }
  if (!built_) rebuild();
  int wait = -1;
  auto soonest = [&](int ms) { wait = wait < 0 ? ms : std::min(wait, ms); };
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
    const std::string path = cur && !cur->isUntitled() ? cur->path().generic_string() : std::string();
    if (path != trackedFile_) {
      trackedFile_ = path;
      if (!path.empty()) reveal(cur->path());
    }
  }
  return wait;
}

}  // namespace tf::ui
