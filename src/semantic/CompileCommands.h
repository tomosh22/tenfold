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
  static bool save(const std::vector<CompileCommand>& cmds, const std::filesystem::path& jsonFile,
                   std::string* error = nullptr);
};

namespace compdb {

// Runs cmake to configure `root` into `buildDir` with compile command export.
// Returns the path to the generated compile_commands.json.
std::optional<std::filesystem::path> fromCMake(const std::filesystem::path& root, const std::filesystem::path& buildDir,
                                               std::string* log = nullptr);

// One entry of the solution's "Configuration|Platform" list (e.g. "Debug" + "x64", "Release" + "Any CPU").
struct SolutionConfiguration {
  std::string configuration;
  std::string platform;
  std::string name() const { return configuration + "|" + platform; }
  bool operator==(const SolutionConfiguration& o) const;  // case-insensitive, like MSBuild
  bool operator!=(const SolutionConfiguration& o) const { return !(*this == o); }
};

struct VcxprojOptions {
  // Project configuration used by fromVcxproj. fromSolution treats these as the requested *solution* configuration
  // and maps it per project (see pickSolutionConfiguration / readSolution).
  std::string configuration = "Debug";
  std::string platform = "x64";
  std::filesystem::path solutionDir;   // for $(SolutionDir); defaults to project dir
  std::filesystem::path solutionFile;  // for $(SolutionPath)/$(SolutionName); set by fromSolution
  std::map<std::string, std::string> extraProperties;
  // Environment variables are visible as properties (lowest priority). `environment` entries win over the process
  // environment; `useProcessEnvironment = false` makes the evaluation hermetic (tests).
  std::map<std::string, std::string> environment;
  bool useProcessEnvironment = true;
  // Directory.Build.props / Directory.Build.targets are imported the way MSBuild does (nearest file walking up).
  bool directoryBuildFiles = true;
  // Defines that MSBuild/cl add implicitly (_WIN32, _WIN64, _DEBUG, _DLL, _MT, UNICODE, _MBCS). See ARCHITECTURE.md.
  bool implicitDefines = true;
};

// A project listed in a .sln.
struct SolutionProject {
  std::string name;
  std::string guid;  // upper-case, with braces
  std::string typeGuid;
  std::filesystem::path path;  // absolute, lexically normalised
  bool isVcxproj() const;
};
// What a solution maps a solution configuration to for one project.
struct SolutionProjectConfiguration {
  std::string configuration;  // the project's own configuration / platform ("Debug", "Win32")
  std::string platform;
  bool build = false;  // .Build.0 present: the project is part of the build in this configuration
};
struct SolutionInfo {
  std::filesystem::path path;
  std::vector<SolutionProject> projects;
  std::vector<SolutionConfiguration> configurations;  // GlobalSection(SolutionConfigurationPlatforms), in file order
  // True when the file has a GlobalSection(ProjectConfigurationPlatforms). Without one (hand-written solutions) every
  // project is built in the selected configuration under the same name.
  bool hasProjectConfigurationMap = false;
  // guid -> lower-case "config|platform" of the solution -> mapping (.ActiveCfg / .Build.0 lines).
  std::map<std::string, std::map<std::string, SolutionProjectConfiguration>> projectConfigurations;
  // The mapping for `project` in `solutionConfig`; nullopt when the solution does not map it (project skipped).
  std::optional<SolutionProjectConfiguration> mapping(const SolutionProject& project,
                                                      const SolutionConfiguration& solutionConfig) const;
};
SolutionInfo readSolution(const std::filesystem::path& sln, std::string* error = nullptr);
std::vector<SolutionConfiguration> solutionConfigurations(const std::filesystem::path& sln,
                                                          std::string* error = nullptr);
// Choose the active configuration from `available`: an exact (case-insensitive) match of configuration+platform; else
// Debug|x64; else the first entry. With nothing available the request is returned unchanged. `exact` (optional) tells
// whether the request itself was found.
SolutionConfiguration pickSolutionConfiguration(const std::vector<SolutionConfiguration>& available,
                                                const std::string& configuration, const std::string& platform,
                                                bool* exact = nullptr);
// Parse "Debug|x64" (the platform may be missing: "Debug").
SolutionConfiguration parseSolutionConfiguration(const std::string& name);

// Projects referenced by a .sln (absolute paths, .vcxproj only; ignores the configuration mapping).
std::vector<std::filesystem::path> projectsInSolution(const std::filesystem::path& sln, std::string* error = nullptr);
// Evaluate a .vcxproj (following .props imports, Directory.Build.props/targets) into compile commands.
std::vector<CompileCommand> fromVcxproj(const std::filesystem::path& vcxproj, const VcxprojOptions& opt,
                                        std::string* error = nullptr);
// Evaluate every project the solution builds in the configuration picked from `opt` (pickSolutionConfiguration),
// each with its mapped project configuration/platform. Projects the solution does not build there are skipped.
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
// containing a header is used (capped). With `clDriver` the commands use the
// clang-cl driver (argv[0] "clang-cl", /std:, /EHsc, /c) so clangd finds the
// MSVC STL and Windows SDK headers (INCLUDE / Visual Studio detection); the
// default is GCC-style clang/clang++. -I flags are spelled "-I" in both.
std::vector<CompileCommand> heuristic(const std::filesystem::path& root,
                                      const std::vector<std::filesystem::path>& sources,
                                      const std::vector<std::filesystem::path>& headers,
                                      const std::string& std = "c++20",
                                      const std::vector<std::string>& includePaths = {}, bool clDriver = false);

// Flags a standalone clangd needs to find the system headers of this host
// when a compile command does not carry them: "-isysroot <SDK>" on macOS
// (SDKROOT, else `xcrun --show-sdk-path`); empty elsewhere.
std::vector<std::string> platformSystemFlags();

// ---- MSVC toolchain discovery (for clangd --query-driver) ----
// Inputs are injected so the logic runs on any host against a fake tree.
struct MsvcProbe {
  std::string vcToolsInstallDir;         // value of %VCToolsInstallDir% ("" if unset)
  std::function<std::string()> vswhere;  // returns vswhere's installationPath output ("" if unavailable)
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

// MSBuild-style property expansion: "$(Name)" -> value (unknown -> ""). Names are case-insensitive. A few property
// functions are understood ([MSBuild]::EnsureTrailingSlash, NormalizeDirectory, GetDirectoryNameOfFileAbove, ...);
// anything else ($(Registry:...), $(x.Replace(..))) expands to "".
std::string expandProperties(const std::string& s, const std::map<std::string, std::string>& props);
// MSBuild condition evaluation for the common forms: 'a'=='b', 'a'!='b', Exists('x'), and/or/!.
bool evaluateCondition(const std::string& cond, const std::map<std::string, std::string>& props,
                       const std::filesystem::path& baseDir);

}  // namespace compdb
}  // namespace tf
