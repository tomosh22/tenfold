#include "semantic/FastIndexStore.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>

#include "core/PathUtil.h"
#include "core/Platform.h"

namespace tf {

static_assert(std::endian::native == std::endian::little, "store format is little-endian; add byte swapping for BE");

namespace {

constexpr char kMagic[8] = {'T', 'F', 'F', 'I', 'D', 'X', 0, 1};
constexpr size_t kHeaderSize = 48;
constexpr size_t kSymbolRecord = 4 + 4 + 4 + 4 + 1 + 1 + 1;
constexpr size_t kFileRecord = 4 + 8 + 8 + 4 + 4 + 4;
constexpr size_t kPpCounts = 4 * 4;  // nArms, nSymArm, nMacros, nBodies
constexpr size_t kArmRecord = 4 + 4 + 4 + 1 + 4;
constexpr size_t kSymArmRecord = 4 + 4;
constexpr size_t kMacroRecord = 4 + 4 + 4 + 4 + 1;
constexpr uint32_t kExtractorSalt =
    6;  // 6: preprocessor conditionals (arms, per-symbol arm, #define / #undef records, integer bodies).
        // 5: minArity, variadic and fileLocal (static) per symbol, `static int x;` in a class is a
        // declaration. 4: NEL / LS / PS count as line breaks in token positions (stored lines of
        // files that have them were off). 3: arity in the flags byte, operator names,
        // macros behind comments, friends, typedef bodies. 2: BOM skipped, lone CR
        // ends lines, C keywords for .c
        // bump by hand for changes the probe below cannot observe

uint64_t hashBytes(const char* p, size_t n) {
  uint64_t h = 0x9E3779B97F4A7C15ull ^ n;
  auto mix = [&](uint64_t v) {
    h ^= v;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 32;
  };
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    uint64_t v;
    std::memcpy(&v, p + i, 8);
    mix(v);
  }
  uint64_t tail = 0;
  if (i < n) std::memcpy(&tail, p + i, n - i);
  mix(tail);
  return h;
}

template <class T>
void put(std::string& out, T v) {
  out.append(reinterpret_cast<const char*>(&v), sizeof(T));
}

struct Reader {
  const char* p;
  size_t n;
  size_t pos = 0;
  bool ok = true;
  size_t left() const { return n - pos; }
  template <class T>
  T get() {
    T v{};
    if (!ok || left() < sizeof(T)) {
      ok = false;
      return v;
    }
    std::memcpy(&v, p + pos, sizeof(T));
    pos += sizeof(T);
    return v;
  }
};

bool fail(std::string* err, const char* msg) {
  if (err) *err = msg;
  return false;
}

}  // namespace

uint32_t FastIndexStore::extractorFingerprint() {
  static const uint32_t fp = [] {
    static const char probe[] = R"cpp(
#include <vector>
#include "a/b.h"
#define MAC(x) (x)
      namespace n1::n2 {
      template <class T>
      class C : public B {
       public:
        C();
        ~C();
        T get() const;
        int f;
        static int g;
      };
      struct S {
        enum class E { A, B = 2 };
        using U = int;
        typedef long L;
        virtual void v() = 0;
      };
      union Un {
        int a;
      };
      void C<int>::m(int a, char* b) {
        auto x = [&] { return 1; };
      }
      int free_fn(int a = (1, 2)) noexcept;
      extern int var;
      }  // namespace n1::n2
      concept Cc = true;
      enum Old { O1, O2 };
      operator bool() const;
      static int sv;
      struct SD {
        static int sd;
        static const int ic = 1;
      };
      static void sf(int a, int b = 1);
      void vf(const char* f, ...);
      template <class... A>
      void pk(int a, A... r);
#ifndef GUARD_H
#define GUARD_H
#if defined(A) && B > 1
      int cond_a;
#elif !defined X
      int cond_b;
#else
      int cond_c;
#endif
#ifdef Q
#define LVL (1 + 2)
#undef LVL
#endif
#endif
    )cpp";
    FileSymbols probed;
    extractFile(probe, probed);
    auto& syms = probed.symbols;
    auto& inc = probed.includes;
    std::string blob;
    put<uint32_t>(blob, kExtractorSalt);
    put<uint32_t>(blob, kFormatVersion);
    for (auto& s : syms) {
      blob += s.name;
      blob += '\0';
      blob += s.scope;
      blob += '\0';
      put<int32_t>(blob, static_cast<int32_t>(s.kind));
      put<int32_t>(blob, s.line);
      put<int32_t>(blob, s.col);
      put<uint8_t>(blob, static_cast<uint8_t>(s.definition));
      put<uint8_t>(blob, static_cast<uint8_t>(s.isTemplate));
      put<int8_t>(blob, s.arity);
      put<int8_t>(blob, s.minArity);
      put<uint8_t>(blob, static_cast<uint8_t>((s.fileLocal ? 1 : 0) | (s.variadic ? 2 : 0)));
    }
    for (size_t k = 0; k < inc.size(); ++k) {
      blob += inc[k];
      blob += (k < probed.angled.size() && probed.angled[k]) ? '\1' : '\0';
    }
    for (auto& a : probed.arms) {
      put<uint8_t>(blob, static_cast<uint8_t>(a.kind));
      put<uint32_t>(blob, a.parent);
      put<uint32_t>(blob, a.prev);
      put<int32_t>(blob, a.line);
      blob += a.expr;
      blob += '\0';
    }
    for (auto& sa : probed.symArm) {
      put<uint32_t>(blob, sa.first);
      put<uint32_t>(blob, sa.second);
    }
    for (auto& m : probed.macros) {
      blob += m.name;
      blob += '\0';
      put<int32_t>(blob, m.line);
      put<uint32_t>(blob, m.arm);
      put<int32_t>(blob, m.body);
      put<uint8_t>(blob, static_cast<uint8_t>((m.undef ? 1 : 0) | (m.functionLike ? 2 : 0)));
    }
    for (auto& b : probed.bodies) {
      blob += b;
      blob += '\0';
    }
    uint64_t h = hashBytes(blob.data(), blob.size());
    return static_cast<uint32_t>(h ^ (h >> 32));
  }();
  return fp;
}

std::string FastIndexStore::serialize(const Entries& files) {
  // Intern every string once.
  std::unordered_map<std::string_view, uint32_t> ids;
  std::vector<std::string_view> table;
  std::vector<std::string> pathStrings;  // UTF-8 path strings must outlive the views
  pathStrings.reserve(files.size());
  auto intern = [&](std::string_view s) -> uint32_t {
    auto [it, inserted] = ids.try_emplace(s, static_cast<uint32_t>(table.size()));
    if (inserted) table.push_back(s);
    return it->second;
  };
  size_t symTotal = 0;
  for (auto& f : files) symTotal += f->symbols.size();
  ids.reserve(symTotal / 2 + files.size() + 16);

  std::string body;
  body.reserve(symTotal * kSymbolRecord + files.size() * 32);
  for (auto& f : files) {
    pathStrings.push_back(pathKeyUtf8(f->file));
    put<uint32_t>(body, intern(pathStrings.back()));
    put<int64_t>(body, f->mtime);
    put<uint64_t>(body, f->size);
    put<uint32_t>(body, static_cast<uint32_t>(f->symbols.size()));
    put<uint32_t>(body, static_cast<uint32_t>(f->includes.size()));
    for (auto& s : f->symbols) {
      put<uint32_t>(body, intern(s.name));
      put<uint32_t>(body, intern(s.scope));
      put<int32_t>(body, s.line);
      put<int32_t>(body, s.col);
      put<uint8_t>(body, static_cast<uint8_t>(s.kind));
      // bit 0 definition, bit 1 template, bits 2..7 arity + 1 (0 = not a function / unknown)
      put<uint8_t>(body, static_cast<uint8_t>((s.definition ? 1 : 0) | (s.isTemplate ? 2 : 0) | ((s.arity + 1) << 2)));
      // bits 0..5 minArity + 1 (0 = unknown), bit 6 fileLocal, bit 7 variadic
      put<uint8_t>(body, static_cast<uint8_t>((s.minArity + 1) | (s.fileLocal ? 64 : 0) | (s.variadic ? 128 : 0)));
    }
    for (size_t k = 0; k < f->includes.size(); ++k)  // bit 31: written with angle brackets
      put<uint32_t>(body, intern(f->includes[k]) | ((k < f->angled.size() && f->angled[k]) ? 0x80000000u : 0u));
    const bool pp = !f->arms.empty() || !f->macros.empty();
    put<uint32_t>(body, pp ? 1 : 0);
    if (pp) {
      put<uint32_t>(body, static_cast<uint32_t>(f->arms.size()));
      put<uint32_t>(body, static_cast<uint32_t>(f->symArm.size()));
      put<uint32_t>(body, static_cast<uint32_t>(f->macros.size()));
      put<uint32_t>(body, static_cast<uint32_t>(f->bodies.size()));
      for (auto& a : f->arms) {
        put<uint32_t>(body, a.parent);
        put<uint32_t>(body, a.prev);
        put<int32_t>(body, a.line);
        put<uint8_t>(body, static_cast<uint8_t>(a.kind));
        put<uint32_t>(body, intern(a.expr));
      }
      for (auto& sa : f->symArm) {
        put<uint32_t>(body, sa.first);
        put<uint32_t>(body, sa.second);
      }
      for (auto& m : f->macros) {
        put<uint32_t>(body, intern(m.name));
        put<int32_t>(body, m.line);
        put<uint32_t>(body, m.arm);
        put<int32_t>(body, m.body);
        put<uint8_t>(body, static_cast<uint8_t>((m.undef ? 1 : 0) | (m.functionLike ? 2 : 0)));
      }
      for (auto& b : f->bodies) put<uint32_t>(body, intern(b));
    }
  }

  std::string payload;
  size_t blobBytes = 0;
  for (auto sv : table) blobBytes += sv.size();
  payload.reserve(table.size() * 4 + blobBytes + body.size());
  for (auto sv : table) put<uint32_t>(payload, static_cast<uint32_t>(sv.size()));
  for (auto sv : table) payload.append(sv.data(), sv.size());
  payload += body;

  std::string out;
  out.reserve(kHeaderSize + payload.size());
  out.append(kMagic, sizeof(kMagic));
  put<uint32_t>(out, kFormatVersion);
  put<uint32_t>(out, extractorFingerprint());
  put<uint64_t>(out, payload.size());
  put<uint64_t>(out, hashBytes(payload.data(), payload.size()));
  put<uint32_t>(out, static_cast<uint32_t>(files.size()));
  put<uint32_t>(out, static_cast<uint32_t>(table.size()));
  put<uint64_t>(out, 0);
  out += payload;
  return out;
}

std::optional<FastIndexStore::Entries> FastIndexStore::deserialize(const char* data, size_t size, std::string* err) {
  auto bad = [&](const char* msg) -> std::optional<Entries> {
    fail(err, msg);
    return std::nullopt;
  };
  if (size < kHeaderSize) return bad("truncated header");
  if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0) return bad("bad magic");
  Reader h{data, kHeaderSize, sizeof(kMagic)};
  uint32_t version = h.get<uint32_t>();
  uint32_t extractor = h.get<uint32_t>();
  uint64_t payloadSize = h.get<uint64_t>();
  uint64_t payloadHash = h.get<uint64_t>();
  uint32_t fileCount = h.get<uint32_t>();
  uint32_t stringCount = h.get<uint32_t>();
  if (version != kFormatVersion) return bad("format version mismatch");
  if (extractor != extractorFingerprint()) return bad("extractor version mismatch");
  if (payloadSize != size - kHeaderSize) return bad("size mismatch (truncated or trailing data)");
  const char* payload = data + kHeaderSize;
  if (hashBytes(payload, payloadSize) != payloadHash) return bad("checksum mismatch");

  Reader r{payload, static_cast<size_t>(payloadSize)};
  // Reject absurd counts before allocating anything proportional to them.
  if (stringCount > r.left() / 4) return bad("bad string count");
  std::vector<uint32_t> lens(stringCount);
  uint64_t blobBytes = 0;
  for (auto& l : lens) {
    l = r.get<uint32_t>();
    blobBytes += l;
  }
  if (!r.ok || blobBytes > r.left()) return bad("bad string table");
  std::vector<std::string_view> strs(stringCount);
  const char* blob = payload + r.pos;
  size_t off = 0;
  for (uint32_t i = 0; i < stringCount; ++i) {
    strs[i] = std::string_view(blob + off, lens[i]);
    off += lens[i];
  }
  r.pos += static_cast<size_t>(blobBytes);
  if (fileCount > r.left() / kFileRecord) return bad("bad file count");

  // Pass 1 (sequential, cheap): validate the per-file framing and record where each file's records start.
  std::vector<size_t> starts(fileCount);
  for (uint32_t fi = 0; fi < fileCount; ++fi) {
    starts[fi] = r.pos;
    r.get<uint32_t>();
    r.get<int64_t>();
    r.get<uint64_t>();
    uint32_t nsym = r.get<uint32_t>();
    uint32_t ninc = r.get<uint32_t>();
    if (!r.ok) return bad("bad file record");
    if (nsym > r.left() / kSymbolRecord || ninc > (r.left() - size_t(nsym) * kSymbolRecord) / 4)
      return bad("bad counts");
    r.pos += size_t(nsym) * kSymbolRecord + size_t(ninc) * 4;
    const uint32_t hasPp = r.get<uint32_t>();
    if (!r.ok || hasPp > 1) return bad("bad file record");
    if (hasPp) {
      const uint32_t na = r.get<uint32_t>(), ns = r.get<uint32_t>(), nm = r.get<uint32_t>(), nb = r.get<uint32_t>();
      if (!r.ok) return bad("bad file record");
      const size_t need =
          size_t(na) * kArmRecord + size_t(ns) * kSymArmRecord + size_t(nm) * kMacroRecord + size_t(nb) * 4;
      if (na > r.left() / kArmRecord || ns > r.left() / kSymArmRecord || nm > r.left() / kMacroRecord ||
          nb > r.left() / 4 || need > r.left())
        return bad("bad counts");
      r.pos += need;
    }
  }
  if (r.left() != 0) return bad("trailing data");

  // Pass 2 (parallel): decode the files. Every field is still range-checked.
  Entries out(fileCount);
  std::atomic<uint32_t> next{0};
  std::atomic<bool> failed{false};
  auto worker = [&] {
    while (!failed.load(std::memory_order_relaxed)) {
      uint32_t fi = next.fetch_add(1);
      if (fi >= fileCount) break;
      Reader fr{payload, static_cast<size_t>(payloadSize), starts[fi]};
      auto fs = std::make_shared<FileSymbols>();
      uint32_t path = fr.get<uint32_t>();
      fs->mtime = fr.get<int64_t>();
      fs->size = fr.get<uint64_t>();
      uint32_t nsym = fr.get<uint32_t>();
      uint32_t ninc = fr.get<uint32_t>();
      if (!fr.ok || path >= stringCount || nsym > fr.left() / kSymbolRecord) {
        failed = true;
        return;
      }
      fs->file = pathFromUtf8(std::string(strs[path]));
      fs->symbols.resize(nsym);
      for (auto& s : fs->symbols) {
        uint32_t name = fr.get<uint32_t>();
        uint32_t scope = fr.get<uint32_t>();
        s.line = fr.get<int32_t>();
        s.col = fr.get<int32_t>();
        uint8_t kind = fr.get<uint8_t>();
        uint8_t flags = fr.get<uint8_t>();
        uint8_t extra = fr.get<uint8_t>();
        if (!fr.ok || name >= stringCount || scope >= stringCount || kind > static_cast<uint8_t>(SymbolKind::Unknown)) {
          failed = true;
          return;
        }
        s.name.assign(strs[name]);
        s.scope.assign(strs[scope]);
        s.kind = static_cast<SymbolKind>(kind);
        s.definition = flags & 1;
        s.isTemplate = flags & 2;
        s.arity = static_cast<int8_t>((flags >> 2) - 1);
        s.minArity = static_cast<int8_t>((extra & 63) - 1);
        s.fileLocal = extra & 64;
        s.variadic = extra & 128;
      }
      fs->includes.resize(ninc);
      fs->angled.assign(ninc, 0);
      for (uint32_t k = 0; k < ninc; ++k) {
        const uint32_t raw = fr.get<uint32_t>();
        const uint32_t id = raw & 0x7fffffffu;
        if (!fr.ok || id >= stringCount) {
          failed = true;
          return;
        }
        fs->includes[k].assign(strs[id]);
        fs->angled[k] = (raw >> 31) ? 1 : 0;
      }
      if (fr.get<uint32_t>() != 0) {
        const uint32_t na = fr.get<uint32_t>(), ns = fr.get<uint32_t>(), nm = fr.get<uint32_t>(),
                       nb = fr.get<uint32_t>();
        if (!fr.ok) {
          failed = true;
          return;
        }
        fs->arms.resize(na);
        uint32_t idx = 0;
        for (auto& a : fs->arms) {
          ++idx;
          a.parent = fr.get<uint32_t>();
          a.prev = fr.get<uint32_t>();
          a.line = fr.get<int32_t>();
          const uint8_t kind = fr.get<uint8_t>();
          const uint32_t expr = fr.get<uint32_t>();
          // ids only point backwards: the evaluator relies on the arm tables being acyclic
          if (!fr.ok || kind > static_cast<uint8_t>(CondArm::Never) || expr >= stringCount || a.parent >= idx ||
              a.prev >= idx) {
            failed = true;
            return;
          }
          a.kind = static_cast<CondArm::Kind>(kind);
          a.expr.assign(strs[expr]);
        }
        fs->symArm.resize(ns);
        int64_t lastSym = -1;
        for (auto& sa : fs->symArm) {
          sa.first = fr.get<uint32_t>();
          sa.second = fr.get<uint32_t>();
          if (!fr.ok || sa.first >= nsym || static_cast<int64_t>(sa.first) <= lastSym || sa.second == 0 ||
              sa.second > na) {
            failed = true;
            return;
          }
          lastSym = sa.first;
        }
        fs->macros.resize(nm);
        for (auto& m : fs->macros) {
          const uint32_t name = fr.get<uint32_t>();
          m.line = fr.get<int32_t>();
          m.arm = fr.get<uint32_t>();
          m.body = fr.get<int32_t>();
          const uint8_t fl = fr.get<uint8_t>();
          if (!fr.ok || name >= stringCount || m.arm > na || m.body < -1 || m.body >= static_cast<int64_t>(nb)) {
            failed = true;
            return;
          }
          m.name.assign(strs[name]);
          m.undef = fl & 1;
          m.functionLike = fl & 2;
        }
        fs->bodies.resize(nb);
        for (auto& b : fs->bodies) {
          const uint32_t id = fr.get<uint32_t>();
          if (!fr.ok || id >= stringCount) {
            failed = true;
            return;
          }
          b.assign(strs[id]);
        }
      }
      out[fi] = std::move(fs);
    }
  };
  unsigned nthreads = fileCount < 256 ? 1 : std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
  if (nthreads == 1) {
    worker();
  } else {
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < nthreads; ++t) pool.emplace_back(worker);
    for (auto& th : pool) th.join();
  }
  if (failed) return bad("bad file or symbol record");
  return out;
}

bool FastIndexStore::save(const std::filesystem::path& path, const Entries& files, std::string* err) {
  std::string bytes = serialize(files);
  std::error_code ec;
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
  std::filesystem::path tmp = path;
  tmp += ".tmp." + std::to_string(tf::currentProcessId());
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return fail(err, "cannot create temp file");
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) {
      out.close();
      std::filesystem::remove(tmp, ec);
      return fail(err, "write failed");
    }
  }
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::filesystem::remove(tmp, ec);
    return fail(err, "rename failed");
  }
  return true;
}

std::optional<FastIndexStore::Entries> FastIndexStore::load(const std::filesystem::path& path, std::string* err) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    fail(err, "cannot open store");
    return std::nullopt;
  }
  std::streamoff sz = in.tellg();
  if (sz < 0) {
    fail(err, "cannot size store");
    return std::nullopt;
  }
  std::string bytes(static_cast<size_t>(sz), '\0');
  in.seekg(0);
  in.read(bytes.data(), sz);
  if (!in) {
    fail(err, "read failed");
    return std::nullopt;
  }
  return deserialize(bytes.data(), bytes.size(), err);
}

}  // namespace tf
