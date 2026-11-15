#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace tf::projedit {

// PROJ-ADD (docs/VS_BEHAVIOUR.md section 20): adding a new file to a Visual Studio project the way Solution Explorer's
// Add > New Item does. The project file is edited as TEXT, not re-serialised: everything that is not the new element
// (encoding and byte order mark, line endings, indentation, comments, element order) stays byte for byte. Nothing here
// touches the disk until commit(); planAdd() is pure apart from reading the two files.

enum class Support {
  Edit,          // a .vcxproj: items are listed one by one, the file is edited
  NoEditNeeded,  // an SDK-style project (`<Project Sdk="...">`): files are included by globs, the project is unchanged
  Unsupported,   // any other project format: tenfold reads its items but does not write them
};

// How a project is handled; `why` (when given) says in a few words for Unsupported / NoEditNeeded.
Support classify(const std::filesystem::path& project, std::string* why = nullptr);

// The item type Visual Studio gives a new file of this name: ClCompile (C and C++ sources), ClInclude (headers),
// ResourceCompile (.rc), None (everything else).
std::string itemTypeFor(const std::filesystem::path& file);

// "" when `fileName` can be the Include of a project item, else why not (MSBuild's special characters ; % $ @ * ?
// would need escaping, and tenfold's reader of project files does not unescape).
std::string nameProblem(const std::string& fileName);

// The Include value of `file` in `project`: relative to the project folder, with backslashes (as Visual Studio writes
// it); absolute only when no relative form exists (another drive).
std::string includeFor(const std::filesystem::path& project, const std::filesystem::path& file);

struct AddRequest {
  std::filesystem::path project;  // the .vcxproj
  std::filesystem::path file;     // the new file (it need not exist yet)
  std::string filter;             // "Source Files\\Sub" when the file goes into a filter of the .filters file; else ""
};

struct AddPlan {
  std::filesystem::path project;
  std::filesystem::path filters;  // <project>.filters
  std::string itemType;
  std::string include;
  // New contents; empty when that file does not change (the item is already listed / there is nothing to write).
  bool editProject = false, editFilters = false;
  std::string projectText, filtersText;
  std::string projectOld, filtersOld;  // what was read (the rollback)
  // The files commit() will write.
  std::vector<std::filesystem::path> files() const;
};

// Reads the project (and its .filters file when the request has a filter and the file exists) and computes the new
// texts. False with `error` when the project cannot be read, is UTF-16 or has no recognisable <Project> element.
// An item the project already lists (same Include, any case, either slash) makes editProject false.
bool planAdd(const AddRequest& req, AddPlan& plan, std::string* error);

// Whether the file can be written (the read-only attribute / permission bits and an open for writing).
bool isWritable(const std::filesystem::path& file);
// The files of the plan that are not writable.
std::vector<std::filesystem::path> unwritable(const AddPlan& plan);

// Writes the plan. Refuses (error set, nothing written) when a file is not writable; when the second write fails the
// first file is restored.
bool commit(const AddPlan& plan, std::string* error);

}  // namespace tf::projedit
