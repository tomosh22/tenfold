#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace tf::ui {

// Things the editor asks the operating system to open (W8b): a web address in the default browser and a file in the
// platform's file manager. EditorApp holds one; tests replace the functions so no browser or file manager starts.
struct OsLauncher {
  // False (with *err set when given) if the program could not be started.
  std::function<bool(const std::string& url, std::string* err)> openUrl;
  // The file manager showing `file`: Windows `explorer /select,<file>`, macOS `open -R <file>`, elsewhere `xdg-open`
  // on the file's folder (there is no portable "select").
  std::function<bool(const std::filesystem::path& file, std::string* err)> showInFolder;
  // Any program with arguments (PerforceShowInP4v starts p4v this way); left running on its own. `exe` without a
  // directory is searched in PATH.
  std::function<bool(const std::string& exe, const std::vector<std::string>& args, std::string* err)> startProgram;
};

// The real thing, through tf::Process. The started programs are left running on their own (a launcher such as
// xdg-open hands over and exits; it is reaped on the next launch).
OsLauncher systemLauncher();

// Percent-encoding of `text` for a URL query: unreserved characters (letters, digits, - . _ ~) stay, a blank becomes
// %20, every other byte %XX (UTF-8 bytes included).
std::string urlEncode(const std::string& text);

// InternetSearch: `command` (setting editor.internetSearchCommand, a URL ending where the search word goes) followed
// by the encoded `text`. Surrounding blanks and line breaks of `text` are dropped, inner runs of them become one blank;
// empty text gives an empty string.
std::string internetSearchUrl(const std::string& command, const std::string& text);

}  // namespace tf::ui
