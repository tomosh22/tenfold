#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "semantic/FastIndex.h"

namespace tf {

// Persistent cache of the fast index (`<root>/.tenfold/fastindex.bin`).
//
// File format (all integers little-endian, fixed width):
//   header (48 bytes)
//     char[8]  magic "TFFIDX\0\1"
//     u32      formatVersion      (kFormatVersion; bump when the layout below changes)
//     u32      extractorVersion   (extractorFingerprint(); changes when the lexer / extractor behaviour changes)
//     u64      payloadSize        (bytes after the header; detects truncation)
//     u64      payloadHash        (64-bit hash of the payload; detects corruption)
//     u32      fileCount
//     u32      stringCount
//     u64      reserved (0)
//   payload
//     u32 lengths[stringCount]    then the concatenated string bytes (one interned blob; every path, name,
//                                 scope and include string is stored once and referenced by index)
//     per file: u32 path, i64 mtime, u64 size, u32 nSymbols, u32 nIncludes,
//               nSymbols * { u32 name, u32 scope, i32 line, i32 col, u8 kind, u8 flags(bit0 definition, bit1 template) },
//               nIncludes * u32
//
// Every read is bounds-checked; any inconsistency (bad magic/version, size or hash mismatch, index out of range,
// trailing bytes) makes load() fail so the caller falls back to a full re-index. Paths are stored as given
// (absolute generic paths), so moving a workspace simply invalidates the entries.
class FastIndexStore {
 public:
  static constexpr uint32_t kFormatVersion = 1;
  using Entries = std::vector<std::shared_ptr<const FileSymbols>>;

  // Hash of a built-in probe run through extractSymbols() combined with a manual salt, so any change to the
  // lexer/extractor output automatically invalidates old stores. Computed once per process.
  static uint32_t extractorFingerprint();

  // Writes atomically: to "<path>.tmp.<pid>" then rename. Creates parent directories. Returns false (and sets
  // *err) on any I/O error; the existing store is left untouched in that case.
  static bool save(const std::filesystem::path& path, const Entries& files, std::string* err = nullptr);

  // nullopt (and *err set) when the file is missing, corrupt, truncated or written by another format/extractor.
  static std::optional<Entries> load(const std::filesystem::path& path, std::string* err = nullptr);

  // In-memory variants (used by save/load and by tests that mutate bytes).
  static std::string serialize(const Entries& files);
  static std::optional<Entries> deserialize(const char* data, size_t size, std::string* err = nullptr);
};

}  // namespace tf
