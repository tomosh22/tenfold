#include "semantic/TuContext.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <set>

#include "core/PathUtil.h"

namespace tf::tu {

namespace fs = std::filesystem;

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\f' || c == '\v' || c == '\r'; }

std::string lowerCopy(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Advances the comment / literal state over [b, e) of one physical line. Returns the new "inside /* */" state.
bool advanceState(std::string_view line, size_t b, bool inBlock) {
  size_t i = b;
  while (i < line.size()) {
    if (inBlock) {
      const size_t e = line.find("*/", i);
      if (e == std::string_view::npos) return true;
      i = e + 2;
      inBlock = false;
      continue;
    }
    const char c = line[i];
    if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') return false;
    if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
      inBlock = true;
      i += 2;
      continue;
    }
    if (c == '"' || c == '\'') {
      ++i;
      while (i < line.size() && line[i] != c) i += line[i] == '\\' ? 2 : 1;
      ++i;
      continue;
    }
    ++i;
  }
  return inBlock;
}

}  // namespace

std::vector<IncludeDirective> scanIncludeDirectives(std::string_view text) {
  std::vector<IncludeDirective> out;
  bool inBlock = false;
  int depth = 0;
  size_t pos = 0;
  int lineNo = 0;
  while (pos <= text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(pos, end - pos);
    const size_t lineStart = pos;
    const int firstLine = lineNo;
    size_t curEnd = end;
    int consumed = 1;
    bool directive = false;
    if (!inBlock) {
      size_t i = 0;
      while (i < line.size() && isSpace(line[i])) ++i;
      directive = i < line.size() && line[i] == '#';
    }
    if (directive) {
      std::string joined(line);
      auto continued = [&] {
        size_t n = joined.size();
        if (n && joined[n - 1] == '\r') --n;
        return n && joined[n - 1] == '\\';
      };
      while (continued() && curEnd < text.size()) {
        while (!joined.empty() && (joined.back() == '\r' || joined.back() == '\\')) joined.pop_back();
        size_t e2 = text.find('\n', curEnd + 1);
        if (e2 == std::string_view::npos) e2 = text.size();
        joined.append(text.substr(curEnd + 1, e2 - curEnd - 1));
        curEnd = e2;
        ++consumed;
      }
      std::string_view d = joined;
      size_t k = d.find('#') + 1;
      while (k < d.size() && isSpace(d[k])) ++k;
      size_t w = k;
      while (w < d.size() && std::isalpha(static_cast<unsigned char>(d[w]))) ++w;
      const std::string_view word = d.substr(k, w - k);
      if (word == "if" || word == "ifdef" || word == "ifndef") {
        ++depth;
      } else if (word == "endif") {
        if (depth > 0) --depth;
      } else if (word == "include" || word == "include_next" || word == "import") {
        size_t q = w;
        while (q < d.size() && isSpace(d[q])) ++q;
        if (q < d.size() && (d[q] == '"' || d[q] == '<')) {
          const char close = d[q] == '"' ? '"' : '>';
          const size_t e = d.find(close, q + 1);
          if (e != std::string_view::npos) {
            IncludeDirective inc;
            inc.line = firstLine;
            inc.offset = lineStart;
            inc.spelling = std::string(d.substr(q + 1, e - q - 1));
            inc.angled = close == '>';
            inc.condDepth = depth;
            out.push_back(std::move(inc));
          }
        }
      }
      inBlock = advanceState(d, 0, false);
    } else {
      inBlock = advanceState(line, 0, inBlock);
    }
    if (curEnd >= text.size()) break;
    pos = curEnd + 1;
    lineNo += consumed;
  }
  return out;
}

bool includeSpellingResolves(const std::string& spelling, const fs::path& includer, const fs::path& target) {
  if (spelling.empty()) return false;
  const fs::path sp = fs::path(spelling).lexically_normal();
  if (sp.empty() || sp.is_absolute()) return pathKey(sp) == pathKey(target.lexically_normal());
  const fs::path tn = target.lexically_normal();
  const fs::path near = (includer.parent_path() / sp).lexically_normal();
  if (pathKey(near) == pathKey(tn)) return true;
  // Include path: the target's path ends with the spelling.
  const std::string ts = pathKey(pathKeyUtf8(tn));
  std::string ss = pathKey(pathKeyUtf8(sp));
  if (ss.rfind("../", 0) == 0 || ss == "..") return false;
  if (ts.size() <= ss.size() || ts.compare(ts.size() - ss.size(), ss.size(), ss) != 0 ||
      ts[ts.size() - ss.size() - 1] != '/')
    return false;
  std::error_code ec;
  if (fs::exists(near, ec) && pathKey(near) != pathKey(tn)) return false;  // the quoted lookup finds another file first
  return true;
}

std::string prefixBefore(std::string_view text, const IncludeDirective& d) {
  std::string out(text.substr(0, std::min(d.offset, text.size())));
  if (!out.empty() && out.back() != '\n') out += '\n';
  for (int i = 0; i < d.condDepth; ++i) out += "#endif\n";
  return out;
}

bool endsInsideBody(std::string_view t) {
  std::vector<bool> nsLike;  // one entry per open brace
  size_t boundary = 0;
  bool lineStart = true;
  for (size_t i = 0; i < t.size(); ++i) {
    const char c = t[i];
    if (c == '\n') {
      lineStart = true;
      continue;
    }
    if (lineStart && (c == ' ' || c == '\t' || c == '\r')) continue;
    if (lineStart && c == '#') {  // preprocessor line, with continuations
      while (i < t.size() && t[i] != '\n') {
        if (t[i] == '\\' && i + 1 < t.size() && (t[i + 1] == '\n' || (t[i + 1] == '\r' && i + 2 < t.size()))) {
          i += t[i + 1] == '\r' ? 2 : 1;
        }
        ++i;
      }
      lineStart = true;
      continue;
    }
    lineStart = false;
    if (c == '/' && i + 1 < t.size() && t[i + 1] == '/') {
      while (i < t.size() && t[i] != '\n') ++i;
      lineStart = true;
    } else if (c == '/' && i + 1 < t.size() && t[i + 1] == '*') {
      const size_t e = t.find("*/", i + 2);
      i = e == std::string_view::npos ? t.size() : e + 1;
    } else if (c == '"' || c == '\'') {
      ++i;
      while (i < t.size() && t[i] != c && t[i] != '\n') i += t[i] == '\\' ? 2 : 1;
    } else if (c == ';') {
      boundary = i + 1;
    } else if (c == '{') {
      const std::string_view stmt = t.substr(boundary, i - boundary);
      auto hasWord = [&](std::string_view w) {
        for (size_t p = stmt.find(w); p != std::string_view::npos; p = stmt.find(w, p + 1)) {
          const bool l = p == 0 || !(std::isalnum(static_cast<unsigned char>(stmt[p - 1])) || stmt[p - 1] == '_');
          const size_t e = p + w.size();
          const bool r = e >= stmt.size() || !(std::isalnum(static_cast<unsigned char>(stmt[e])) || stmt[e] == '_');
          if (l && r) return true;
        }
        return false;
      };
      nsLike.push_back(hasWord("namespace") || hasWord("extern"));
      boundary = i + 1;
    } else if (c == '}') {
      if (!nsLike.empty()) nsLike.pop_back();
      boundary = i + 1;
    }
  }
  for (bool ns : nsLike)
    if (!ns) return true;
  return false;
}

bool isStandardHeaderExtension(const fs::path& p) {
  const std::string e = lowerCopy(pathUtf8(p.extension()));
  return e == ".h" || e == ".hh" || e == ".hpp" || e == ".hxx" || e == ".h++" || e == ".hp" || e == ".cuh";
}

std::optional<Plan> planContext(const fs::path& orphan, const Env& env, int maxDepth) {
  if (!env.includersOf || !env.readText || !env.commandFor) return std::nullopt;
  struct Node {
    fs::path file;
    int parent = -1;
    IncludeDirective directive;  // in `file`: the directive that includes the parent
    std::string text;
  };
  std::vector<Node> nodes;
  nodes.push_back({orphan, -1, {}, {}});
  std::set<std::string> seen{pathKey(orphan)};
  constexpr size_t kMaxNodes = 400;
  std::deque<std::pair<size_t, int>> queue{{0, 0}};
  while (!queue.empty()) {
    auto [idx, depth] = queue.front();
    queue.pop_front();
    if (depth >= maxDepth) continue;
    const fs::path target = nodes[idx].file;
    std::vector<fs::path> cands = env.includersOf(target);
    std::sort(cands.begin(), cands.end(), [](const fs::path& a, const fs::path& b) { return pathKey(a) < pathKey(b); });
    // Files with a compile command first: reaching one ends the search at this level.
    std::stable_sort(cands.begin(), cands.end(), [&](const fs::path& a, const fs::path& b) {
      return (env.commandFor(a) != nullptr) > (env.commandFor(b) != nullptr);
    });
    for (const fs::path& f : cands) {
      if (nodes.size() >= kMaxNodes) break;
      if (!seen.insert(pathKey(f)).second) continue;
      std::string text;
      if (!env.readText(f, text)) continue;
      const IncludeDirective* hit = nullptr;
      std::vector<IncludeDirective> dirs = scanIncludeDirectives(text);
      for (const auto& d : dirs) {
        if (includeSpellingResolves(d.spelling, f, target)) {
          hit = &d;
          break;
        }
      }
      if (!hit) continue;
      Node n;
      n.file = f;
      n.parent = static_cast<int>(idx);
      n.directive = *hit;
      n.text = std::move(text);
      nodes.push_back(std::move(n));
      const size_t me = nodes.size() - 1;
      if (const CompileCommand* cc = env.commandFor(f)) {
        Plan plan;
        plan.root = *cc;
        for (int cur = static_cast<int>(me); cur > 0; cur = nodes[static_cast<size_t>(cur)].parent) {
          const Node& nd = nodes[static_cast<size_t>(cur)];
          Link l;
          l.file = nd.file;
          l.directive = nd.directive;
          l.prefix = prefixBefore(nd.text, nd.directive);
          plan.fragment = plan.fragment || endsInsideBody(l.prefix);
          plan.links.push_back(std::move(l));
        }
        // The orphan includes a file of its own chain: it brings its own context.
        std::string otext;
        if (env.readText(orphan, otext)) {
          for (const auto& d : scanIncludeDirectives(otext))
            for (const auto& l : plan.links)
              if (includeSpellingResolves(d.spelling, orphan, l.file)) return std::nullopt;
        }
        return plan;
      }
      queue.push_back({me, depth + 1});
    }
  }
  return std::nullopt;
}

std::vector<std::string> buildArguments(const Plan& plan, const fs::path& orphan,
                                        const std::vector<fs::path>& prefixFiles, const CommandOptions& opt) {
  std::vector<std::string> in = plan.root.arguments;
  std::vector<std::string> out;
  if (in.empty()) in.push_back(opt.clMode ? "clang-cl" : (opt.isC ? "clang" : "clang++"));
  // Find the main file argument.
  const fs::path rootFile = plan.root.file;
  size_t fileIdx = in.size();
  for (size_t i = 1; i < in.size(); ++i) {
    if (in[i].empty() || in[i][0] == '-' || (opt.clMode && in[i][0] == '/' && in[i].size() < 4)) continue;
    fs::path a(in[i]);
    if (a.is_relative()) a = plan.root.directory / a;
    if (pathKey(a.lexically_normal()) == pathKey(rootFile.lexically_normal())) {
      fileIdx = i;
      break;
    }
  }
  // Arguments of the root that must not survive: the output, dependency files, and a precompiled header would
  // describe the root's translation unit.
  auto dropsNext = [](const std::string& a) { return a == "-o" || a == "-MF" || a == "-MT" || a == "-MQ"; };
  for (size_t i = 0; i < in.size(); ++i) {
    if (i == fileIdx) continue;
    if (i > 0 && dropsNext(in[i])) {
      ++i;
      continue;
    }
    out.push_back(in[i]);
  }
  for (size_t i = 0; i < plan.links.size() && i < prefixFiles.size(); ++i) {
    if (opt.clMode) {
      out.push_back("/FI" + pathUtf8(prefixFiles[i]));
      out.push_back("/I" + pathUtf8(plan.links[i].file.parent_path()));
    } else {
      out.push_back("-include");
      out.push_back(pathUtf8(prefixFiles[i]));
      out.push_back("-iquote");
      out.push_back(pathUtf8(plan.links[i].file.parent_path()));
    }
  }
  if (opt.clMode) {
    out.push_back(opt.isC ? "/TC" : "/TP");
  } else {
    out.push_back("-x");
    const std::string lang = opt.isC ? "c" : "c++";
    out.push_back(opt.headerLike ? lang + "-header" : lang);
  }
  out.push_back(pathUtf8(orphan));
  return out;
}

}  // namespace tf::tu
