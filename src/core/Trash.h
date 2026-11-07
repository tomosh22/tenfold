#pragma once
#include <filesystem>
#include <string>

namespace tf {

// Moving a file or folder to the operating system's trash instead of deleting it (the workspace explorer's Delete).
//  * Windows: the Recycle Bin (SHFileOperationW with FOF_ALLOWUNDO).
//  * Linux / other POSIX except macOS: the freedesktop.org Trash specification: $XDG_DATA_HOME/Trash (default
//    ~/.local/share/Trash) with files/ and info/<name>.trashinfo (Path= percent-encoded, DeletionDate=). A path on
//    another file system than that trash goes to the top directory's $topdir/.Trash-$uid. A name already in the trash
//    becomes "name.2", "name.3", ...
//  * macOS: not supported (NSFileManager trashItemAtURL needs Objective-C); trashSupported is false and the caller
//    deletes permanently, saying so.
// trashSupported says, before anything is done, whether moveToTrash can work for `path`; the caller words its
// confirmation from it. moveToTrash never deletes permanently.
bool trashSupported(const std::filesystem::path& path);
bool moveToTrash(const std::filesystem::path& path, std::string* error = nullptr);

#ifndef _WIN32
// The freedesktop implementation with the data home given (tests point it at a temp directory; empty = the default).
// `trashedTo` receives the new location of the item.
bool trashSupportedIn(const std::filesystem::path& path, const std::filesystem::path& dataHome);
bool moveToTrashIn(const std::filesystem::path& path, const std::filesystem::path& dataHome, std::string* error,
                   std::filesystem::path* trashedTo = nullptr);
#endif

}  // namespace tf
