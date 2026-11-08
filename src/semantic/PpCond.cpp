#include "semantic/PpCond.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace tf::pp {

using Kind = MacroState::Kind;

// ---------------------------------------------------------------------------------------------------------------------
// Extraction
// ---------------------------------------------------------------------------------------------------------------------

bool isExpressionBody(std::string_view body) {
  if (body.empty() || body.size() > 48) return false;
  for (char c : body) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u) || c == '_' || c == '$' || u >= 0x80) continue;
    if (c == '\0' || std::strchr(" +-*/%<>=!&|^~?:()'", c) == nullptr) return false;
  }
  return true;
}

namespace {

constexpr size_t kMaxArms = 1u << 16;  // per file; conditionals beyond it are unconditional (never dropped)
constexpr size_t kMaxBodies = 1u << 14;
constexpr size_t kMaxNesting = 512;  // conditionals nested deeper are unconditional

// The name of `!defined(X)` / `!defined X` (spaces ignored), else "".
std::string notDefinedName(std::string_view e) {
  std::string s;
  for (char c : e)
    if (c != ' ' && c != '\t') s += c;
  if (s.rfind("!defined", 0) != 0) return {};
  std::string_view rest = std::string_view(s).substr(8);
  if (rest.size() >= 3 && rest.front() == '(' && rest.back() == ')') rest = rest.substr(1, rest.size() - 2);
  if (rest.empty() || firstIdentifier(rest).size() != rest.size()) return {};
  return std::string(rest);
}

// Does a `#define NAME` directive follow right after token k (the next directive of any kind)?
bool definesNext(const std::vector<Token>& toks, size_t k, const std::string& name) {
  for (size_t j = k + 1; j < toks.size(); ++j) {
    if (toks[j].kind != Tok::Preprocessor) continue;
    const Directive d = parseDirective(toks[j].text);
    if (d.kind != Directive::Kind::Define) return false;
    const auto def = parseDefine(d.arg);
    return def && def->name == name;
  }
  return false;
}

// Names of the directives that need a full parse (others are #include / #pragma / #error ...).
bool interesting(std::string_view text) {
  size_t i = 1;
  while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
  if (i >= text.size()) return false;
  if (!std::isalpha(static_cast<unsigned char>(text[i]))) return true;  // a comment before the name: parse it fully
  const char c = text[i];
  return c == 'i' ? (text.compare(i, 2, "if") == 0) : (c == 'e' || c == 'd' || c == 'u');
}

}  // namespace

void scanPreprocessor(const std::vector<Token>& toks, FileSymbols& out) {
  out.arms.clear();
  out.symArm.clear();
  out.macros.clear();
  out.bodies.clear();
  struct Frame {
    uint32_t parentArm = 0;
    uint32_t lastArm = 0;
    bool guard = false;     // an include guard: unconditional, its #else / #elif arms never run
    bool overflow = false;  // arm table full: stays unconditional
  };
  std::vector<Frame> stack;
  std::vector<std::pair<int, uint32_t>> timeline;  // (line, arm in force from that line on)
  uint32_t cur = 0;
  auto addArm = [&](CondArm::Kind kind, uint32_t parent, uint32_t prev, std::string expr, int line) -> uint32_t {
    if (out.arms.size() >= kMaxArms) return 0;
    CondArm a;
    a.kind = kind;
    a.parent = parent;
    a.prev = prev;
    a.line = line;
    a.expr = std::move(expr);
    out.arms.push_back(std::move(a));
    return static_cast<uint32_t>(out.arms.size());
  };
  for (size_t k = 0; k < toks.size(); ++k) {
    const Token& t = toks[k];
    if (t.kind != Tok::Preprocessor || !interesting(t.text)) continue;
    const Directive d = parseDirective(t.text);
    using DK = Directive::Kind;
    switch (d.kind) {
      case DK::If:
      case DK::Ifdef:
      case DK::Ifndef: {
        Frame f;
        f.parentArm = cur;
        if (stack.size() >= kMaxNesting) {
          f.overflow = true;
          stack.push_back(f);
          timeline.emplace_back(t.line, cur);
          break;
        }
        std::string guardName;
        if (d.kind == DK::Ifndef) guardName = firstIdentifier(d.arg);
        else if (d.kind == DK::If) guardName = notDefinedName(d.arg);
        if (!guardName.empty() && definesNext(toks, k, guardName)) {
          f.guard = true;
        } else {
          const CondArm::Kind kind =
              d.kind == DK::If ? CondArm::If : (d.kind == DK::Ifdef ? CondArm::Ifdef : CondArm::Ifndef);
          std::string expr = d.kind == DK::If ? d.arg : firstIdentifier(d.arg);
          const uint32_t id = (d.kind != DK::If && expr.empty()) ? 0 : addArm(kind, cur, 0, std::move(expr), t.line);
          if (id == 0) f.overflow = true;
          else {
            f.lastArm = id;
            cur = id;
          }
        }
        stack.push_back(f);
        timeline.emplace_back(t.line, cur);
        break;
      }
      case DK::Elif:
      case DK::Elifdef:
      case DK::Elifndef:
      case DK::Else: {
        if (stack.empty()) break;
        Frame& f = stack.back();
        if (f.overflow) {
          cur = f.parentArm;
        } else if (f.guard) {
          cur = addArm(CondArm::Never, f.parentArm, 0, {}, t.line);
          if (cur == 0) cur = f.parentArm;
        } else {
          std::string expr;
          CondArm::Kind kind = CondArm::Else;
          if (d.kind == DK::Elif) {
            kind = CondArm::Elif;
            expr = d.arg;
          } else if (d.kind == DK::Elifdef || d.kind == DK::Elifndef) {
            kind = CondArm::Elif;
            expr = (d.kind == DK::Elifndef ? "!defined(" : "defined(") + firstIdentifier(d.arg) + ")";
          }
          const uint32_t id = addArm(kind, f.parentArm, f.lastArm, std::move(expr), t.line);
          if (id == 0) {
            f.overflow = true;
            cur = f.parentArm;
          } else {
            f.lastArm = id;
            cur = id;
          }
        }
        timeline.emplace_back(t.line, cur);
        break;
      }
      case DK::Endif: {
        if (stack.empty()) break;
        cur = stack.back().parentArm;
        stack.pop_back();
        timeline.emplace_back(t.line, cur);
        break;
      }
      case DK::Define:
      case DK::Undef: {
        MacroDef m;
        m.line = t.line;
        m.arm = cur;
        if (d.kind == DK::Undef) {
          m.name = firstIdentifier(d.arg);
          m.undef = true;
        } else if (auto def = parseDefine(d.arg)) {
          m.name = std::move(def->name);
          m.functionLike = def->functionLike;
          if (!def->functionLike && isExpressionBody(def->body) && out.bodies.size() < kMaxBodies) {
            m.body = static_cast<int32_t>(out.bodies.size());
            out.bodies.push_back(std::move(def->body));
          }
        }
        if (!m.name.empty()) out.macros.push_back(std::move(m));
        break;
      }
      default: break;
    }
  }
  if (out.arms.empty()) return;
  for (size_t i = 0; i < out.symbols.size(); ++i) {
    const int line = out.symbols[i].line;
    auto it = std::upper_bound(timeline.begin(), timeline.end(), line,
                               [](int l, const std::pair<int, uint32_t>& e) { return l < e.first; });
    if (it == timeline.begin()) continue;
    const uint32_t arm = std::prev(it)->second;
    if (arm != 0) out.symArm.emplace_back(static_cast<uint32_t>(i), arm);
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Compile commands
// ---------------------------------------------------------------------------------------------------------------------

Host Host::current() {
  Host h;
#if defined(_WIN32)
  h.os = Os::Windows;
#elif defined(__APPLE__)
  h.os = Os::Mac;
#elif defined(__linux__)
  h.os = Os::Linux;
#endif
#if defined(__x86_64__) || defined(_M_X64)
  h.arch = Arch::X64;
#elif defined(__i386__) || defined(_M_IX86)
  h.arch = Arch::X86;
#elif defined(__aarch64__) || defined(_M_ARM64)
  h.arch = Arch::Arm64;
#endif
  return h;
}

namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$'; }

bool isNameChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$'; }

enum class Driver { Other, Cl, ClangCl, Gcc, Clang };

struct DriverInfo {
  Driver kind = Driver::Other;
  Host::Os os = Host::Os::Unknown;  // from a target prefix of the driver name (`x86_64-linux-gnu-g++`), if any
  bool plain = false;               // no target prefix: it runs for the host unless --target says otherwise
};

Host::Os osOfTriple(const std::string& t) {
  if (t.find("windows") != std::string::npos || t.find("mingw") != std::string::npos ||
      t.find("msvc") != std::string::npos || t.find("win32") != std::string::npos)
    return Host::Os::Windows;
  if (t.find("linux") != std::string::npos) return Host::Os::Linux;
  if (t.find("darwin") != std::string::npos || t.find("apple") != std::string::npos ||
      t.find("macos") != std::string::npos)
    return Host::Os::Mac;
  return Host::Os::Unknown;
}

Host::Arch archOfTriple(const std::string& t) {
  const std::string a = t.substr(0, t.find('-'));
  if (a == "x86_64" || a == "amd64" || a == "x64") return Host::Arch::X64;
  if (a.size() == 4 && a[0] == 'i' && a[2] == '8' && a[3] == '6') return Host::Arch::X86;
  if (a == "aarch64" || a == "arm64") return Host::Arch::Arm64;
  return Host::Arch::Unknown;
}

DriverInfo classifyDriver(const std::string& arg0) {
  std::string name = arg0;
  const size_t slash = name.find_last_of("/\\");
  if (slash != std::string::npos) name = name.substr(slash + 1);
  name = lower(name);
  if (name.size() > 4 && name.compare(name.size() - 4, 4, ".exe") == 0) name.resize(name.size() - 4);
  DriverInfo d;
  if (name == "cl") {
    d.kind = Driver::Cl;
    d.os = Host::Os::Windows;
    d.plain = true;
    return d;
  }
  if (name == "clang-cl" || startsWith(name, "clang-cl-")) {
    d.kind = Driver::ClangCl;
    d.plain = true;
    return d;
  }
  // strip a version suffix: g++-12, clang-17, gcc-13.2
  std::string base = name;
  while (!base.empty() && (std::isdigit(static_cast<unsigned char>(base.back())) || base.back() == '.'))
    base.pop_back();
  if (base.size() < name.size() && !base.empty() && base.back() == '-') base.pop_back();
  else base = name;
  auto endsWith = [&](const char* suf) {
    const std::string s = suf;
    return base.size() >= s.size() && base.compare(base.size() - s.size(), s.size(), s) == 0;
  };
  Driver kind = Driver::Other;
  size_t sufLen = 0;
  if (endsWith("clang++")) kind = Driver::Clang, sufLen = 7;
  else if (endsWith("clang")) kind = Driver::Clang, sufLen = 5;
  else if (endsWith("g++")) kind = Driver::Gcc, sufLen = 3;
  else if (endsWith("gcc")) kind = Driver::Gcc, sufLen = 3;
  else if (base == "cc" || base == "c++") kind = Driver::Gcc, sufLen = base.size();  // cc / c++: gcc or clang
  if (kind == Driver::Other) return d;
  d.kind = kind;
  if (base == "cc" || base == "c++") {
    d.kind = Driver::Other;  // unknown which compiler: no compiler-specific macros, but the OS follows the host
    d.plain = true;
    return d;
  }
  std::string prefix = base.substr(0, base.size() - sufLen);
  if (prefix.empty()) {
    d.plain = true;
  } else {
    d.os = osOfTriple(prefix);
  }
  return d;
}

// The value of a -D argument `NAME`, `NAME=VAL`, `NAME(args)=VAL`: the name and its state.
bool parseDefineArg(std::string_view v, std::string& name, MacroState& st, std::string& bodyOut) {
  bodyOut.clear();
  if (v.empty() || !isIdentStart(v[0])) return false;
  size_t e = 1;
  while (e < v.size() && isNameChar(v[e])) ++e;
  name = std::string(v.substr(0, e));
  std::string_view rest = v.substr(e);
  if (rest.empty()) {
    st = MacroState::integer(1);  // -DNAME is -DNAME=1
    return true;
  }
  if (rest[0] == '(') {
    st = MacroState::defined();
    return true;
  }
  if (rest[0] != '=') return false;  // `/Dev/x.cpp`: a path, not a define
  const std::string_view body = rest.substr(1);
  if (body.empty()) {
    st = MacroState::defined();
    return true;
  }
  const MacroState val = evalValue(body, [](std::string_view) { return MacroState::unknown(); });
  st = val.kind == Kind::Int ? val : MacroState::defined();
  if (val.kind != Kind::Int && body.size() <= 48) { bodyOut = std::string(body); }
  return true;
}

// Standard version of `-std=c++17` / `/std:c++17`: the value of __cplusplus, 0 = unknown.
int64_t cplusplusOf(std::string v) {
  v = lower(v);
  if (startsWith(v, "gnu++")) v = "c++" + v.substr(5);
  if (!startsWith(v, "c++")) return 0;
  const std::string n = v.substr(3);
  if (n == "98" || n == "03") return 199711;
  if (n == "11" || n == "0x") return 201103;
  if (n == "14" || n == "1y") return 201402;
  if (n == "17" || n == "1z") return 201703;
  if (n == "20" || n == "2a") return 202002;
  if (n == "23" || n == "2b") return 202302;
  return 0;
}

}  // namespace

CommandMacros macrosOfCommand(const CompileCommand& cc, const Host& host) {
  CommandMacros r;
  if (cc.arguments.empty()) return r;
  r.known = true;
  const DriverInfo drv = classifyDriver(cc.arguments[0]);
  const bool clMode = drv.kind == Driver::Cl || drv.kind == Driver::ClangCl;

  std::string triple;
  Host::Arch archFlag = Host::Arch::Unknown;
  std::string stdSpelling;
  int lang = 0;  // 1 = C, 2 = C++
  bool msCompat = false;
  bool debugRuntime = false;                                   // /MDd /MTd /LDd: the debug runtime defines _DEBUG
  std::vector<std::pair<std::string, MacroState>> flagMacros;  // in command-line order
  std::unordered_map<std::string, std::string> flagBodies;

  auto value = [&](const std::string& a, size_t& i, size_t prefixLen) -> std::string {
    if (a.size() > prefixLen) return a.substr(prefixLen);
    return i + 1 < cc.arguments.size() ? cc.arguments[++i] : std::string();
  };
  for (size_t i = 1; i < cc.arguments.size(); ++i) {
    const std::string& a = cc.arguments[i];
    if (a.empty()) continue;
    if (a[0] == '@') {
      r.complete = false;
      continue;
    }
    const bool dash = a[0] == '-', slash = a[0] == '/' && clMode;
    if (!dash && !slash) continue;
    if (a.size() >= 2 && (a[1] == 'D' || a[1] == 'U') && (dash || slash)) {
      size_t j = i;
      const std::string v = value(a, j, 2);
      std::string name, body;
      MacroState st;
      if (a[1] == 'U') {
        const std::string n = firstIdentifier(v);
        if (!n.empty() && n.size() == v.size()) {
          flagMacros.emplace_back(n, MacroState::undefined());
          flagBodies.erase(n);
          i = j;
        }
      } else if (parseDefineArg(v, name, st, body)) {
        flagMacros.emplace_back(name, st);
        if (body.empty()) flagBodies.erase(name);
        else flagBodies[name] = body;
        i = j;
      }
      continue;
    }
    if (dash && a == "-include") {
      if (i + 1 < cc.arguments.size()) r.forcedIncludes.push_back(cc.arguments[++i]);
      continue;
    }
    if (dash && a == "-imacros") {
      r.complete = false;  // macros from a file: not read
      continue;
    }
    if ((dash || slash) && clMode && startsWith(std::string_view(a).substr(1), "FI")) {
      size_t j = i;
      const std::string v = value(a, j, 3);
      if (!v.empty()) {
        r.forcedIncludes.push_back(v);
        i = j;
      }
      continue;
    }
    if (dash && startsWith(a, "--target=")) {
      triple = lower(a.substr(9));
      continue;
    }
    if (dash && a == "-target" && i + 1 < cc.arguments.size()) {
      triple = lower(cc.arguments[++i]);
      continue;
    }
    if (dash && a == "-m64") archFlag = Host::Arch::X64;
    else if (dash && a == "-m32") archFlag = Host::Arch::X86;
    else if (dash && startsWith(a, "-std=")) stdSpelling = a.substr(5);
    else if (slash && startsWith(a, "/std:")) stdSpelling = a.substr(5);
    else if (dash && clMode && startsWith(a, "-std:")) stdSpelling = a.substr(5);
    else if (dash && a == "-x" && i + 1 < cc.arguments.size()) {
      const std::string l = lower(cc.arguments[++i]);
      lang = (l == "c" || l == "c-header") ? 1 : (startsWith(l, "c++") ? 2 : lang);
    } else if (dash && startsWith(a, "-x") && a.size() > 2) {
      const std::string l = lower(a.substr(2));
      lang = (l == "c" || l == "c-header") ? 1 : (startsWith(l, "c++") ? 2 : lang);
    } else if (clMode && (a == "/TC" || a == "-TC" || a == "/Tc" || a == "-Tc")) lang = 1;
    else if (clMode && (a == "/TP" || a == "-TP" || a == "/Tp" || a == "-Tp")) lang = 2;
    else if (dash && (startsWith(a, "-fms-compatibility") || startsWith(a, "-fmsc-version"))) msCompat = true;
    else if (clMode && (dash || slash) && (a.substr(1) == "MDd" || a.substr(1) == "MTd" || a.substr(1) == "LDd"))
      debugRuntime = true;
  }
  if (lang == 0) {
    std::string ext = lower(cc.file.extension().string());
    lang = ext == ".c" ? 1 : 2;
  }
  r.isC = lang == 1;

  // ---- the target ----
  Host::Os os = Host::Os::Unknown;
  Host::Arch arch = Host::Arch::Unknown;
  if (!triple.empty()) {
    os = osOfTriple(triple);
    arch = archOfTriple(triple);
  } else if (drv.os != Host::Os::Unknown) {
    os = drv.os;
  } else if (drv.plain) {
    os = host.os;
  }
  if (arch == Host::Arch::Unknown) arch = archFlag;
  if (arch == Host::Arch::Unknown && triple.empty() && drv.plain && !clMode) arch = host.arch;
  if (arch == Host::Arch::X64 && archFlag == Host::Arch::X86) arch = Host::Arch::X86;

  auto& m = r.macros;
  auto one = [&](const char* n) { m[n] = MacroState::integer(1); };
  auto no = [&](const char* n) { m[n] = MacroState::undefined(); };
  auto some = [&](const char* n) { m[n] = MacroState::defined(); };
  const bool known = drv.kind != Driver::Other;

  if (os == Host::Os::Windows) {
    one("_WIN32");
    no("__linux__"), no("__linux"), no("__gnu_linux__"), no("__APPLE__"), no("__MACH__");
    if (arch == Host::Arch::X64 || arch == Host::Arch::Arm64) one("_WIN64");
    else if (arch == Host::Arch::X86) no("_WIN64");
    if (arch == Host::Arch::X64) {
      one("_M_X64"), one("_M_AMD64"), no("_M_IX86");
    } else if (arch == Host::Arch::X86) {
      one("_M_IX86"), no("_M_X64"), no("_M_AMD64");
    }
    if (arch == Host::Arch::X64 || arch == Host::Arch::X86) no("_M_ARM64");
  } else if (os == Host::Os::Linux) {
    one("__linux__"), one("__linux"), one("__gnu_linux__"), one("__unix__"), one("__unix"), one("__ELF__");
    no("_WIN32"), no("_WIN64"), no("__APPLE__"), no("__MACH__"), no("__CYGWIN__"), no("__MINGW32__");
    no("_M_X64"), no("_M_IX86"), no("_M_AMD64");
    if (arch == Host::Arch::X64 || arch == Host::Arch::Arm64) one("__LP64__"), one("_LP64");
  } else if (os == Host::Os::Mac) {
    one("__APPLE__"), one("__MACH__");
    no("__linux__"), no("__linux"), no("__gnu_linux__"), no("_WIN32"), no("_WIN64"), no("__CYGWIN__");
    no("_M_X64"), no("_M_IX86"), no("_M_AMD64");
    if (arch == Host::Arch::X64 || arch == Host::Arch::Arm64) one("__LP64__"), one("_LP64");
  }
  if (drv.kind != Driver::Cl && known) {
    if (arch == Host::Arch::X64) {
      one("__x86_64__"), one("__x86_64"), one("__amd64__"), one("__amd64"), no("__i386__"), no("__aarch64__");
    } else if (arch == Host::Arch::X86) {
      one("__i386__"), one("__i386"), no("__x86_64__"), no("__x86_64"), no("__amd64__"), no("__aarch64__");
    } else if (arch == Host::Arch::Arm64) {
      one("__aarch64__"), no("__x86_64__"), no("__x86_64"), no("__amd64__"), no("__i386__");
    }
  }
  switch (drv.kind) {
    case Driver::Cl:
      if (debugRuntime) one("_DEBUG");
      else no("_DEBUG");
      some("_MSC_VER"), some("_MSC_FULL_VER"), no("__clang__"), no("__GNUC__"), no("__GNUG__");
      if (!r.isC) some("_MSVC_LANG");
      else no("_MSVC_LANG");
      break;
    case Driver::ClangCl:
      if (debugRuntime) one("_DEBUG");
      else no("_DEBUG");
      some("_MSC_VER"), some("_MSC_FULL_VER"), one("__clang__"), no("__GNUC__"), no("__GNUG__");
      one("__has_include");
      if (!r.isC) some("_MSVC_LANG");
      else no("_MSVC_LANG");
      break;
    case Driver::Gcc:
      some("__GNUC__"), no("__clang__"), no("_MSC_VER"), no("_MSC_FULL_VER"), no("_MSVC_LANG");
      if (r.isC) no("__GNUG__");
      else some("__GNUG__");
      one("__has_include");
      break;
    case Driver::Clang:
      one("__clang__"), some("__GNUC__");
      if (r.isC) no("__GNUG__");
      else some("__GNUG__");
      if (!msCompat) no("_MSC_VER"), no("_MSC_FULL_VER"), no("_MSVC_LANG");
      one("__has_include");
      break;
    case Driver::Other: break;
  }
  if (known || drv.kind == Driver::Other) {
    if (r.isC && known) {
      no("__cplusplus");
    } else if (!r.isC && known) {
      const int64_t v = drv.kind == Driver::Cl ? 0 : cplusplusOf(stdSpelling);
      if (v) m["__cplusplus"] = MacroState::integer(v);
      else some("__cplusplus");
    }
  }
  // -D / -U on top, in order
  for (auto& [n, st] : flagMacros) m[n] = st;
  // `-DCHAIN=VAL` with `-DVAL=2`: the body is expanded where the macro is used, so it sees the final -D / -U set
  for (auto& [n, body] : flagBodies) {
    auto it = m.find(n);
    if (it == m.end() || it->second.kind != Kind::Defined) continue;
    const MacroState v = evalValue(body, [&](std::string_view id) {
      auto f = m.find(std::string(id));
      if (f == m.end() || f->second.kind == Kind::Unknown) return MacroState::unknown();
      return f->second.kind == Kind::Undefined ? MacroState::undefined() : f->second;
    });
    if (v.kind == Kind::Int) it->second = v;
    else r.bodies[n] = body;
  }
  return r;
}

namespace {

bool allUpperDigits(std::string_view s, size_t from) {
  for (size_t i = from; i < s.size(); ++i)
    if (!(std::isupper(static_cast<unsigned char>(s[i])) || std::isdigit(static_cast<unsigned char>(s[i]))))
      return false;
  return true;
}

}  // namespace

bool isPlatformMacroName(std::string_view n) {
  if (n.empty()) return false;
  if (n[0] == '_' || n.find("__") != std::string_view::npos) return true;
  for (char c : n)
    if (std::islower(static_cast<unsigned char>(c))) return true;  // linux, unix, assert, errno, va_start, bool ...
  static const char* const kPrefix[] = {"INT",   "UINT",    "LONG",     "ULONG", "LLONG",   "ULLONG", "SHRT", "USHRT",
                                        "CHAR_", "SCHAR",   "UCHAR",    "SIZE_", "PTRDIFF", "WCHAR",  "WINT", "SIG",
                                        "FLT_",  "DBL_",    "LDBL_",    "FP_",   "M_",      "PRI",    "SCN",  "LC_",
                                        "SEEK_", "EXIT_",   "O_",       "S_",    "FE_",     "RLIMIT", "AF_",  "SO_",
                                        "SOCK_", "IPPROTO", "PTHREAD_", "CLOCK_"};
  for (const char* p : kPrefix)
    if (n.substr(0, std::char_traits<char>::length(p)) == p) return true;
  static const char* const kExact[] = {"NULL",           "EOF",      "BUFSIZ",     "FILENAME_MAX", "FOPEN_MAX",
                                       "TMP_MAX",        "RAND_MAX", "HUGE_VAL",   "INFINITY",     "NAN",
                                       "PATH_MAX",       "NAME_MAX", "MB_LEN_MAX", "MB_CUR_MAX",   "DECIMAL_DIG",
                                       "CLOCKS_PER_SEC", "WIN32",    "WIN64",      "BYTE_ORDER",   "LITTLE_ENDIAN",
                                       "BIG_ENDIAN",     "UNICODE",  "TRUE",       "FALSE",        "WINVER",
                                       "NTDDI_VERSION",  "STRICT",   "POSIX",      "BSD",          "UNIX",
                                       "MSDOS"};
  for (const char* e : kExact)
    if (n == e) return true;
  // errno names: EINVAL, ENOENT ... (E plus capitals, no underscore)
  if (n.size() >= 3 && n.size() <= 12 && n[0] == 'E' && allUpperDigits(n, 1)) return true;
  return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Translation unit
// ---------------------------------------------------------------------------------------------------------------------

void collectFlagNames(const CompileCommand& cc, std::unordered_set<std::string>& out) {
  const auto& a = cc.arguments;
  for (size_t i = 1; i < a.size(); ++i) {
    const std::string& s = a[i];
    if (s.size() < 2 || (s[0] != '-' && s[0] != '/') || (s[1] != 'D' && s[1] != 'U')) continue;
    std::string_view v = std::string_view(s).substr(2);
    if (v.empty() && i + 1 < a.size()) v = a[++i];
    if (v.empty() || !isIdentStart(v[0])) continue;
    size_t e = 1;
    while (e < v.size() && isNameChar(v[e])) ++e;
    if (e < v.size() && v[e] != '=' && v[e] != '(') continue;  // `/Dev/x.cpp`
    out.insert(std::string(v.substr(0, e)));
  }
}

TuEnv::TuEnv(Input in) : in_(std::move(in)) {
  auto add = [&](const std::vector<std::shared_ptr<const FileSymbols>>& files,
                 std::unordered_map<std::string, std::vector<Event>>& into) {
    for (const auto& f : files) {
      if (!f) continue;
      fileSet_.insert(f.get());
      for (uint32_t i = 0; i < f->macros.size(); ++i) into[f->macros[i].name].push_back({f.get(), i});
    }
  };
  add(in_.prelude, prelude_);
  add(in_.files, events_);
}

const std::vector<TuEnv::Event>* TuEnv::preludeEventsOf(const std::string& name) const {
  auto it = prelude_.find(name);
  return it == prelude_.end() ? nullptr : &it->second;
}

const std::string* TuEnv::flagBody(const std::string& name) const {
  auto it = in_.command.bodies.find(name);
  return it == in_.command.bodies.end() ? nullptr : &it->second;
}

const std::vector<TuEnv::Event>* TuEnv::eventsOf(const std::string& name) const {
  auto it = events_.find(name);
  return it == events_.end() ? nullptr : &it->second;
}

MacroState TuEnv::initialState(const std::string& name) const {
  if (!in_.command.known) return MacroState::unknown();  // no flags known: the project may define it any way
  auto it = in_.command.macros.find(name);
  if (it != in_.command.macros.end()) return it->second;
  if (!in_.command.complete) return MacroState::unknown();
  // Absence is "not defined" only where nothing outside what we read could have defined the name:
  //  * not a compiler / system / standard-library macro (isPlatformMacroName), unless the project itself uses it as a
  //    configuration macro (some compile command of the workspace sets it, or some indexed file #defines it);
  //  * no include of the translation unit leads outside the index to a project or third-party header, unless the
  //    name is one the project controls.
  const bool projectMacro =
      (in_.setByFlags && in_.setByFlags(name)) || (in_.definedInWorkspace && in_.definedInWorkspace(name));
  if (name[0] == '_' || name.find("__") != std::string::npos) return MacroState::unknown();
  if (projectMacro) return MacroState::undefined();
  if (isPlatformMacroName(name)) return MacroState::unknown();
  if (in_.foreignIncludes) return MacroState::unknown();
  return MacroState::undefined();
}

MacroState Evaluator::expandBody(const std::string& body, const FileSymbols* f, int line) {
  const MacroState v = evalValue(body, [&](std::string_view n) { return macroAt(std::string(n), f, line); }, nullptr);
  return v.kind == Kind::Int ? v : MacroState::defined();
}

MacroState Evaluator::eventState(const TuEnv::Event& e, const FileSymbols* f, int line) {
  const MacroDef& d = e.file->macros[e.index];
  if (d.undef) return MacroState::undefined();
  if (d.functionLike || d.body < 0 || static_cast<size_t>(d.body) >= e.file->bodies.size())
    return MacroState::defined();
  return expandBody(e.file->bodies[static_cast<size_t>(d.body)], f, line);
}

MacroState Evaluator::macroAt(const std::string& name, const FileSymbols* f, int line) {
  auto ii = initial_.find(name);
  if (ii == initial_.end()) ii = initial_.emplace(name, env_.initialState(name)).first;
  MacroState st = ii->second;
  const auto* pre = env_.preludeEventsOf(name);
  const auto* evs = env_.eventsOf(name);
  const std::string* flagBody = st.kind == Kind::Defined ? env_.flagBody(name) : nullptr;
  if (!pre && !evs && !flagBody) return st;
  budget_ -= 1 + static_cast<int>((evs ? evs->size() : 0) + (pre ? pre->size() : 0));
  if (depth_ >= 8 || budget_ < 0) return MacroState::unknown();  // cycles, or a pathological pile of directives
  ++depth_;
  if (flagBody) st = expandBody(*flagBody, f, line);
  if (pre) {
    // forced includes run first; their directives are in order when they all sit in one file
    bool oneFile = true;
    for (const auto& e : *pre) oneFile = oneFile && e.file == (*pre)[0].file;
    for (const auto& e : *pre) {
      const Tri eff = armActive(*e.file, e.file->macros[e.index].arm);
      if (eff == Tri::False) continue;
      const MacroState es = eventState(e, f, line);
      st = (oneFile && eff == Tri::True) ? es : mergeStates(st, es);
    }
  }
  if (evs) {
    bool allHere = f != nullptr;
    if (allHere)
      for (const auto& e : *evs)
        if (e.file != f) {
          allHere = false;
          break;
        }
    for (const auto& e : *evs) {
      const MacroDef& d = e.file->macros[e.index];
      if (allHere && d.line >= line) break;  // events are in source order within a file
      const Tri eff = armActive(*e.file, d.arm);
      if (eff == Tri::False) continue;
      const MacroState es = eventState(e, f, line);
      // Only when every directive is in the asking file (so its order is known) does an active one replace the
      // state; otherwise the directive may or may not have run before this point.
      st = (allHere && eff == Tri::True) ? es : mergeStates(st, es);
    }
  }
  --depth_;
  return st;
}

Tri Evaluator::ownTruth(const FileSymbols& f, uint32_t arm) {
  const Key key{&f, arm};
  auto it = own_.find(key);
  if (it != own_.end()) return it->second;
  const CondArm& a = f.arms[arm - 1];
  Tri t = Tri::Unknown;
  switch (a.kind) {
    case CondArm::Else: t = Tri::True; break;
    case CondArm::Never: t = Tri::False; break;
    case CondArm::Ifdef:
    case CondArm::Ifndef: {
      const MacroState st = macroAt(a.expr, &f, a.line);
      Tri d = st.kind == Kind::Unknown ? Tri::Unknown : triOf(st.kind != Kind::Undefined);
      t = a.kind == CondArm::Ifdef ? d : triNot(d);
      break;
    }
    case CondArm::If:
    case CondArm::Elif: {
      HasIncludeFn hi;
      if (env_.input().hasInclude)
        hi = [&](std::string_view spelling, bool angled) {
          const std::string key = (angled ? "<" : "\"") + std::string(spelling) + "|" + f.file.parent_path().string();
          auto it = hasInclude_.find(key);
          if (it == hasInclude_.end())
            it = hasInclude_.emplace(key, env_.input().hasInclude(std::string(spelling), angled, f.file.parent_path()))
                     .first;
          return it->second;
        };
      t = evalCondition(a.expr, [&](std::string_view n) { return macroAt(std::string(n), &f, a.line); }, &hi);
    } break;
  }
  own_.emplace(key, t);
  return t;
}

// True when none of the earlier arms of the group of `arm` has a true condition. Memoised along the group so that a
// long #elif chain costs O(length) once, not per arm.
Tri Evaluator::noneBefore(const FileSymbols& f, uint32_t arm) {
  std::vector<uint32_t> todo;  // arms whose value is still to compute, latest first
  uint32_t x = arm;
  Tri acc = Tri::True;
  while (true) {
    auto it = none_.find({&f, x});
    if (it != none_.end()) {
      acc = it->second;
      break;
    }
    const uint32_t p = f.arms[x - 1].prev;
    if (p == 0 || p >= x) {  // first arm of its group (ids only point backwards)
      none_.emplace(Key{&f, x}, Tri::True);
      acc = Tri::True;
      break;
    }
    todo.push_back(x);
    x = p;
  }
  // `acc` is none(x); walk forward: none(next) = none(x) and not own(x)
  while (!todo.empty()) {
    acc = triAnd(acc, triNot(ownTruth(f, x)));
    x = todo.back();
    todo.pop_back();
    none_.emplace(Key{&f, x}, acc);
  }
  return acc;
}

Tri Evaluator::armActive(const FileSymbols& f, uint32_t arm) {
  if (arm == 0 || arm > f.arms.size()) return Tri::True;
  const Key key{&f, arm};
  auto it = active_.find(key);
  if (it != active_.end()) return it->second;
  const CondArm& a = f.arms[arm - 1];
  Tri t = a.parent && a.parent != arm ? armActive(f, a.parent) : Tri::True;
  if (t != Tri::False) t = triAnd(t, noneBefore(f, arm));
  if (t != Tri::False) t = triAnd(t, ownTruth(f, arm));
  active_.emplace(key, t);
  return t;
}

}  // namespace tf::pp
