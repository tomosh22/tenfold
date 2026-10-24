#pragma once
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tf {

struct CompileCommand {
  std::filesystem::path directory;
  std::filesystem::path file;
  std::vector<std::string> arguments;  // argv including the compiler
};

// Discovery and generation of compile_commands.json, which is what makes
// clangd exact. Sources, in order of fidelity:
//   1. an existing compile_commands.json (root, build/, out/, ...)
//   2. CMake (configure with CMAKE_EXPORT_COMPILE_COMMANDS)
//   3. Visual Studio .sln / .vcxproj (parsed here, emitted as clang-cl commands)
//   4. heuristic: every source file, -I for every directory containing headers
class CompileDatabase {
 public:
  static std::optional<std::filesystem::path> find(const std::filesystem::path& root);
  static std::vector<CompileCommand> load(const std::filesystem::path& jsonFile, std::string* error = nullptr);
  static bool save(const std::vector<CompileCommand>& cmds, const std::filesystem::path& jsonFile, std::string* error = nullptr);
};

namespace compdb {

// Runs cmake to configure `root` into `buildDir` with compile command export.
// Returns the path to the generated compile_commands.json.
std::optional<std::filesystem::path> fromCMake(const std::filesystem::path& root, const std::filesystem::path& buildDir,
                                               std::string* log = nullptr);

struct VcxprojOptions {
  std::string configuration = "Debug";
  std::string platform = "x64";
  std::filesystem::path solutionDir;  // for $(SolutionDir); defaults to project dir
  std::map<std::string, std::string> extraProperties;
};
// Projects referenced by a .sln (absolute paths, .vcxproj only).
std::vector<std::filesystem::path> projectsInSolution(const std::filesystem::path& sln, std::string* error = nullptr);
// Evaluate a .vcxproj (following local .props imports) into compile commands.
std::vector<CompileCommand> fromVcxproj(const std::filesystem::path& vcxproj, const VcxprojOptions& opt,
                                        std::string* error = nullptr);
std::vector<CompileCommand> fromSolution(const std::filesystem::path& sln, const VcxprojOptions& opt,
                                         std::string* error = nullptr);

// Infer the smallest set of -I directories under which the given #include
// paths (as written in source) resolve to workspace headers. Greedy set cover;
// includes that resolve relative to the including file need no -I and are
// naturally left uncovered. Returns directories ordered by how many includes
// they satisfy.
std::vector<std::filesystem::path> inferIncludeDirs(const std::vector<std::filesystem::path>& headers,
                                                    const std::vector<std::string>& includePaths);

// Best-effort commands for a bare directory of sources. With `includePaths`
// (from the fast index) the -I set is inferred; otherwise every directory
// containing a header is used (capped).
std::vector<CompileCommand> heuristic(const std::filesystem::path& root, const std::vector<std::filesystem::path>& sources,
                                      const std::vector<std::filesystem::path>& headers, const std::string& std = "c++20",
                                      const std::vector<std::string>& includePaths = {});

// ---- MSVC toolchain discovery (for clangd --query-driver) ----
// Inputs are injected so the logic runs on any host against a fake tree.
struct MsvcProbe {
  std::string vcToolsInstallDir;           // value of %VCToolsInstallDir% ("" if unset)
  std::function<std::string()> vswhere;    // returns vswhere's installationPath output ("" if unavailable)
};
// <toolsDir>/bin/Hostx64/x64/cl.exe if it exists.
std::optional<std::filesystem::path> clExeInToolsDir(const std::filesystem::path& toolsDir);
// <installationPath>/VC/Tools/MSVC/<newest version>/bin/Hostx64/x64/cl.exe.
std::optional<std::filesystem::path> clExeInVsInstall(const std::filesystem::path& installationPath);
// Resolve cl.exe. `configured` (workspace.json "msvcToolchain": a cl.exe path,
// or a directory containing it) wins and skips discovery; otherwise
// VCToolsInstallDir, then vswhere. Returns nullopt if nothing is found.
std::optional<std::filesystem::path> discoverCl(const std::string& configured, const MsvcProbe& probe);
// The probe backed by the real environment and (on Windows) vswhere.exe.
MsvcProbe systemMsvcProbe();
// "--query-driver=<dir of cl.exe>/**"
std::string queryDriverArg(const std::filesystem::path& clExe);
// clangd arguments contributed by a known MSVC toolchain (empty if none).
std::vector<std::string> msvcClangdArgs(const std::filesystem::path& clExe);

// MSBuild-style property expansion: "$(Name)" -> value (unknown -> "").
std::string expandProperties(const std::string& s, const std::map<std::string, std::string>& props);
// MSBuild condition evaluation for the common forms: 'a'=='b', 'a'!='b', Exists('x'), and/or/!.
bool evaluateCondition(const std::string& cond, const std::map<std::string, std::string>& props,
                       const std::filesystem::path& baseDir);

}  // namespace compdb
}  // namespace tf
