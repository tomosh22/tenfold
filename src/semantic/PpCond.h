#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "semantic/CompileCommands.h"
#include "semantic/CppLexer.h"
#include "semantic/FastIndex.h"
#include "semantic/PpExpr.h"

namespace tf::pp {

// Preprocessor conditionals of the fast tier: extraction (scanPreprocessor, called by the extractor), what a compile
// command defines (macrosOfCommand), and the evaluator that decides whether a symbol's conditional arm is active in a
// translation unit (TuEnv + Evaluator). Design and the places where absence is treated as false:
// docs/ARCHITECTURE.md, "Preprocessor conditionals in the fast tier (W12-FAST-PP)".

// Fills out.arms, out.symArm, out.macros and out.bodies from the token stream of a file and the symbols already in
// out.symbols (their lines decide the arm). A file without conditionals gets empty tables.
void scanPreprocessor(const std::vector<Token>& toks, FileSymbols& out);

// True for the replacement list of an object-like macro worth keeping: a short expression of numbers, identifiers and
// operators (`3`, `(1 + 2)`, `VAL`, `FEATURE_A && 1`), no strings, braces, semicolons or commas. Evaluated, at the
// `#if` that uses the macro, with the macros known there.
bool isExpressionBody(std::string_view body);

// ---- what a compile command tells
// ------------------------------------------------------------------------------------

struct Host {
  enum class Os : uint8_t { Unknown, Windows, Linux, Mac };
  enum class Arch : uint8_t { Unknown, X64, X86, Arm64 };
  Os os = Os::Unknown;
  Arch arch = Arch::Unknown;
  static Host current();
};

struct CommandMacros {
  bool known = false;    // there is a compile command at all
  bool complete = true;  // every argument was understood (a response file `@x.rsp` is not)
  bool isC = false;      // the translation unit is C
  std::unordered_map<std::string, MacroState> macros;   // -D / -U in order on top of the target's predefined macros
  std::unordered_map<std::string, std::string> bodies;  // -DNAME=BODY whose body names other macros (`-DCHAIN=VAL`)
  std::vector<std::string> forcedIncludes;              // -include / /FI as written
};

// The macros a command defines and undefines (`-DX`, `-DX=V`, `-D X`, `-UX`, `/DX`, `/UX`, `/D X`) and, only where the
// command makes them certain, the predefined macros of its driver and target: `_WIN32` / `_WIN64` / `_M_X64` /
// `_MSC_VER` for cl and clang-cl, `__linux__` / `__unix__` / `__x86_64__` / `__GNUC__` / `__clang__` for a gcc or
// clang driver for Linux, `__APPLE__`, `__cplusplus` (from `-std=` / `/std:`; undefined for C).
CommandMacros macrosOfCommand(const CompileCommand& cc, const Host& host = Host::current());

// The macro names a command's -D / -U (and /D, /U) arguments mention (no predefined macros), added to `out`.
void collectFlagNames(const CompileCommand& cc, std::unordered_set<std::string>& out);

// Names a system or standard header, or the compiler, may define without the project knowing: `_X`, `__x`, names with
// a lower-case letter (`linux`, `unix`, `assert`) and the macros of the C / C++ standard library and the OS headers
// (`NULL`, `INT_MAX`, `EINVAL`, `PATH_MAX`, `WIN32` ...). An undefined such name is Unknown, not Undefined.
bool isPlatformMacroName(std::string_view name);

// ---- translation unit
// ------------------------------------------------------------------------------------------------

class TuEnv {
 public:
  struct Input {
    CommandMacros command;                                  // command.known == false: no compile command
    std::vector<std::shared_ptr<const FileSymbols>> files;  // the files of the translation unit
    // The files of the forced includes (`-include`, `/FI`) and what they include: processed before the main file, so
    // their directives come first and, within one file, in source order.
    std::vector<std::shared_ptr<const FileSymbols>> prelude;
    // `__has_include("x")` / `<x>` seen in `includer`'s directory: whether the header exists (Unknown when that cannot
    // be told: it may be a system header). Null: always Unknown.
    std::function<Tri(const std::string& spelling, bool angled, const std::filesystem::path& includerDir)> hasInclude;
    // Some `#include` of those files resolves to no indexed file and does not name a standard header: a project or
    // third-party header (generated config.h, boost/..., windows.h) may define macros nobody saw.
    bool foreignIncludes = false;
    std::function<bool(const std::string&)> definedInWorkspace;  // some indexed file #defines this name
    std::function<bool(const std::string&)> setByFlags;          // some compile command -D / -U's this name
  };
  explicit TuEnv(Input in);

  const Input& input() const { return in_; }
  // Is `f` one of the files of the translation unit?
  bool contains(const FileSymbols* f) const { return fileSet_.count(f) != 0; }
  struct Event {
    const FileSymbols* file;
    uint32_t index;  // into file->macros
  };
  // The #define / #undef directives of `name` in the translation unit's files.
  const std::vector<Event>* eventsOf(const std::string& name) const;
  const std::vector<Event>* preludeEventsOf(const std::string& name) const;
  // The body of `-DNAME=BODY` when it is an expression over other macros, else null.
  const std::string* flagBody(const std::string& name) const;
  // The state of a macro before any directive of the files ran: the compile command and the target, else the
  // absence rules (see PpCond.cpp initialState).
  MacroState initialState(const std::string& name) const;

 private:
  Input in_;
  std::unordered_map<std::string, std::vector<Event>> events_, prelude_;
  std::unordered_set<const FileSymbols*> fileSet_;
};

// One evaluation session over a TuEnv (not thread safe; the environment is). Memoises arms and macro states.
class Evaluator {
 public:
  explicit Evaluator(const TuEnv& env) : env_(env) {}
  // Is the arm active when `f` is compiled in this translation unit? Unknown whenever it cannot be told.
  Tri armActive(const FileSymbols& f, uint32_t arm);
  // The state of `name` at `line` of file `f` (nullptr: before everything).
  MacroState macroAt(const std::string& name, const FileSymbols* f, int line);

 private:
  Tri ownTruth(const FileSymbols& f, uint32_t arm);
  Tri noneBefore(const FileSymbols& f, uint32_t arm);
  // `f` / `line`: where the macro is being asked about (a body is expanded there).
  MacroState eventState(const TuEnv::Event& e, const FileSymbols* f, int line);
  MacroState expandBody(const std::string& body, const FileSymbols* f, int line);
  const TuEnv& env_;
  int depth_ = 0;
  int budget_ = 200000;  // directive replays per session; beyond it every macro with directives is Unknown
  struct Key {
    const FileSymbols* f;
    uint32_t arm;
    bool operator==(const Key&) const = default;
  };
  struct KeyHash {
    size_t operator()(const Key& k) const { return std::hash<const void*>()(k.f) * 31 + k.arm; }
  };
  std::unordered_map<Key, Tri, KeyHash> active_, own_, none_;
  std::unordered_map<std::string, MacroState> initial_;
  std::unordered_map<std::string, Tri> hasInclude_;
};

}  // namespace tf::pp
