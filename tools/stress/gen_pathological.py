#!/usr/bin/env python3
"""Deterministic generator of pathological workspaces (plan W9f).

  gen_pathological.py --out DIR [--scenario NAME ...] [--scale full|asan|small] [--seed N] [--knob key=value ...]
  gen_pathological.py --list
  gen_pathological.py --churn DIR [--seconds S] [--seed N]     # keeps mutating an existing workspace until stopped

Every scenario is a self-contained workspace written to DIR/<scenario>/ (the directory is wiped first). The same
(seed, scale, knobs) always produce byte-identical files (symlink targets and modes included). Nothing is written
outside DIR. Standard library only.

Scenarios
  tree        many small C++ files; a 10k-file flat dir, a deep chain of dirs, a wide fan-out of dirs
  bigfiles    1M-line C++ file, huge text file, single very long lines, minified one-line C++, 200 MB text file
  nesting     deep braces / namespaces / templates / parens, 64k-char identifiers, 100k declarations in one scope,
              a 50k-alternative macro, a macro with 50k parameters
  encodings   binary with source extensions, invalid UTF-8, UTF-16 (BOM), Latin-1, NUL bytes, CR-only, mixed endings
  fsodd       symlink loops, symlinks outside the workspace, broken links, chmod 000 files / dirs, Unicode and
              space names, paths near PATH_MAX
  truncated   random cuts of real-looking code; files that end mid-token / comment / string / raw string / #if
  mixed       a small healthy project (compile_commands.json, a few real symbols) to check queries stay correct

The `full` scale is the plan's size (about 1.2 GB for everything, 3 GB with the 200 MB file duplicated nowhere);
`asan` is about 1/20th of it, `small` is what the unit tests use (seconds).
"""
import argparse
import os
import random
import shutil
import sys
import time
from pathlib import Path

KNOBS = {
    # name: (full, asan, small)
    "tree_files": (100_000, 5_000, 300),
    "tree_flat": (10_000, 500, 60),
    "tree_depth": (60, 30, 12),
    "tree_fanout_dirs": (2_000, 150, 20),
    "big_lines": (1_000_000, 50_000, 3_000),
    "big_text_mb": (200, 8, 1),
    "long_line_chars": (10_000_000, 1_000_000, 200_000),
    "minified_kb": (4_000, 400, 40),
    "nest_depth": (10_000, 3_000, 600),
    "ident_chars": (64_000, 64_000, 8_000),
    "scope_decls": (100_000, 10_000, 1_500),
    "macro_alts": (50_000, 10_000, 1_000),
    "trunc_files": (400, 100, 40),
    "path_len": (4_000, 3_800, 1_000),
}
SCALE_INDEX = {"full": 0, "asan": 1, "small": 2}


class Cfg:
    def __init__(self, scale, overrides):
        i = SCALE_INDEX[scale]
        self.k = {n: v[i] for n, v in KNOBS.items()}
        for o in overrides or []:
            n, _, v = o.partition("=")
            if n not in self.k:
                sys.exit("unknown knob %r; known: %s" % (n, ", ".join(KNOBS)))
            self.k[n] = int(v)

    def __getitem__(self, n):
        return self.k[n]


def write(p, data):
    p = Path(p)
    p.parent.mkdir(parents=True, exist_ok=True)
    with open(p, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("utf-8"))


def write_chunks(p, chunk_iter):
    p = Path(p)
    p.parent.mkdir(parents=True, exist_ok=True)
    with open(p, "wb") as f:
        for c in chunk_iter:
            f.write(c if isinstance(c, bytes) else c.encode("utf-8"))


# --------------------------------------------------------------------------------------------------------------------
# realistic-looking code
# --------------------------------------------------------------------------------------------------------------------
WORDS = ["Widget", "Node", "Buffer", "Parser", "Cache", "Index", "Visitor", "Handler", "Stream", "Token", "Scope",
         "Entry", "Table", "Queue", "Graph", "Layout", "Shape", "Timer", "Logger", "Pool"]
VERBS = ["update", "resolve", "visit", "flush", "insert", "remove", "lookup", "render", "parse", "reset", "merge"]


def code_unit(rng, n, with_main_ref=True):
    """A plausible small header+source pair as strings: (header, source)."""
    cls = "%s%d" % (rng.choice(WORDS), n)
    ns = "ns%d" % (n % 17)
    v1, v2 = rng.sample(VERBS, 2)
    hdr = f"""#pragma once
#include <string>
#include <vector>
namespace {ns} {{
// {cls}: generated type #{n}.
template <typename T>
struct {cls}Base {{
  T value_{{}};
  virtual ~{cls}Base() = default;
  virtual int {v1}(int x) const = 0;
}};
class {cls} : public {cls}Base<int> {{
 public:
  explicit {cls}(std::string name) : name_(std::move(name)) {{}}
  int {v1}(int x) const override;
  int {v2}(const std::vector<int>& xs);
  static constexpr int kMagic = {n};
 private:
  std::string name_;
  std::vector<int> cache_;
}};
inline int helper_{n}(int a, int b) {{ return a * {n % 97 + 1} + b; }}
}}  // namespace {ns}
"""
    src = f"""#include "{cls}.h"
#include <algorithm>
namespace {ns} {{
int {cls}::{v1}(int x) const {{
  int r = x;
  for (size_t i = 0; i < name_.size(); ++i) r += static_cast<int>(name_[i]) % {n % 7 + 2};
  if (r > {n}) {{ r -= helper_{n}(r, x); }}
  return r;
}}
int {cls}::{v2}(const std::vector<int>& xs) {{
  cache_.assign(xs.begin(), xs.end());
  std::sort(cache_.begin(), cache_.end());
  int s = 0;
  for (int v : cache_) s += {v1}(v);
  return s;
}}
}}  // namespace {ns}
"""
    return cls, hdr, src


REALISTIC = None


def realistic_text(rng, units=6):
    """Several units concatenated into one translation unit (for the truncation scenario)."""
    parts = ["#include <string>\n#include <vector>\n#include <map>\n#define CHECK(x) do { if (!(x)) abort(); } while (0)\n"]
    for i in range(units):
        cls, hdr, src = code_unit(rng, i)
        hdr = hdr.replace("#pragma once\n", "").replace("#include <string>\n#include <vector>\n", "")
        src = src.split("\n", 2)[2]
        parts.append(hdr)
        parts.append(src)
        parts.append(f'const char* kDoc{i} = R"doc(multi\nline "raw" {{ string\n)doc";\n')
        parts.append(f"/* block comment {i}\n   spanning lines */\nauto lam{i} = [](int a) {{ return a + {i}; }};\n")
        parts.append(f'#if defined(FEATURE_{i})\nint feat{i}() {{ return "s\\"tr" ? {i} : 0; }}\n#endif\n')
    return "".join(parts)


# --------------------------------------------------------------------------------------------------------------------
# scenarios
# --------------------------------------------------------------------------------------------------------------------
def sc_tree(root, cfg, rng):
    n = cfg["tree_files"]
    flat, depth, fan = cfg["tree_flat"], cfg["tree_depth"], cfg["tree_fanout_dirs"]
    made = 0
    # 1. flat dir: a header per file only (smallest), alternating .h/.cpp
    for i in range(flat):
        ext = "cpp" if i % 2 else "h"
        write(root / "flat" / f"f{i}.{ext}", f"namespace flat {{ int flat_fn_{i}(int x) {{ return x + {i}; }} }}\n")
        made += 1
    # 2. deep chain
    d = root / "deep"
    for lvl in range(depth):
        d = d / f"level{lvl}"
        write(d / f"deep{lvl}.cpp", f"int deep_fn_{lvl}() {{ return {lvl}; }}\n")
        made += 1
    # 3. wide fan: many dirs with a few files, filling up to the requested total with realistic units
    per = max(1, (n - made) // max(1, fan))
    k = 0
    for di in range(fan):
        for j in range(per):
            if made >= n:
                break
            cls, hdr, src = code_unit(rng, k)
            sub = root / "wide" / f"d{di // 100}" / f"d{di}"
            if j % 2:
                write(sub / f"{cls}.h", hdr)
            else:
                write(sub / f"{cls}.cpp", src)
            made += 1
            k += 1
    write(root / "main.cpp", '#include "wide/d0/d0/Widget0.h"\nint main() { return 0; }\n')


def sc_bigfiles(root, cfg, rng):
    lines = cfg["big_lines"]

    def gen_lines():
        buf = []
        for i in range(lines):
            r = i % 8
            if r == 0:
                buf.append(f"int giant_fn_{i}(int a, int b) {{")
            elif r == 1:
                buf.append(f"  int local_{i} = a * {i % 1000} + b;")
            elif r == 2:
                buf.append(f"  if (local_{i} > {i}) return local_{i} - giant_fn_{max(0, i - 8)}(a, b);")
            elif r == 3:
                buf.append(f'  const char* s = "string {i} // not a comment /* nor this */";')
            elif r == 4:
                buf.append("  // trailing comment line number %d" % i)
            elif r == 5:
                buf.append("  return local_%d;" % (i - 4))
            elif r == 6:
                buf.append("}")
            else:
                buf.append("")
            if len(buf) >= 20000:
                yield "\n".join(buf) + "\n"
                buf = []
        if buf:
            yield "\n".join(buf) + "\n"

    write_chunks(root / "src" / "million_lines.cpp", gen_lines())
    # tail anchors so find / def can be checked
    with open(root / "src" / "million_lines.cpp", "ab") as f:
        f.write(b"int giant_tail_marker() { return giant_fn_0(1, 2); }\n")

    # huge text file (log-like)
    mb = cfg["big_text_mb"]
    line = ("2026-01-01T00:00:00Z INFO worker-%03d processed request id=%08d status=ok bytes=%d latency_ms=%d needle_%d\n")
    target = mb * 1024 * 1024

    def text_chunks():
        done = 0
        i = 0
        while done < target:
            parts = []
            for _ in range(5000):
                parts.append(line % (i % 128, i, (i * 31) % 65536, i % 977, i % 5000))
                i += 1
            s = "".join(parts)
            done += len(s)
            yield s

    write_chunks(root / "data" / "huge.txt", text_chunks())

    # single very long lines
    n = cfg["long_line_chars"]
    expr = "1+" * ((n - 40) // 2)
    write(root / "src" / "oneline_long.cpp", f"int long_expr = {expr}1;\nint after_long_line() {{ return long_expr; }}\n")
    write(root / "src" / "oneline_100k.cpp", "int a = " + "x_" * 50000 + "0;")  # 100k chars, no newline at all
    write(root / "src" / "oneline_comment.cpp", "// " + "word " * (n // 5) + "\nint after_comment() { return 1; }\n")
    write(root / "src" / "oneline_string.cpp", 'const char* s = "' + "ab\\n" * (n // 3) + '";\nint after_string() { return 2; }\n')

    # minified: a real-looking program with all whitespace removed, on one line
    target = cfg["minified_kb"] * 1024
    out = []
    size = 0
    i = 0
    while size < target:
        cls, hdr, src = code_unit(rng, i)
        body = (hdr.split("\n", 2)[2] if False else hdr).replace("#pragma once\n", "")
        body = body.replace("#include <string>\n#include <vector>\n", "")
        piece = "".join(l.strip() for l in body.splitlines() if not l.strip().startswith("//") and "#" not in l)
        out.append(piece)
        size += len(piece)
        i += 1
    write(root / "src" / "minified.cpp",
          '#include <string>\n#include <vector>\n'.replace("\n", " ")[:0] + "".join(out) + "int minified_main(){return 0;}")
    write(root / "src" / "minified.js.h", "/*" + "x" * 1000 + "*/" + "".join(out)[: max(1000, target // 4)])
    write(root / "CMakeLists.txt", "# stress workspace\n")


def sc_nesting(root, cfg, rng):
    n = cfg["nest_depth"]
    src = root / "src"
    write(src / "braces.cpp", "void f() {" + "{" * n + "int innermost_marker = 1;" + "}" * n + "}\nint after_braces() { return 3; }\n")
    write(src / "braces_unbalanced_open.cpp", "void f() {" + "{ if (x) " * n + "\nint after_open() { return 1; }\n")
    write(src / "braces_unbalanced_close.cpp", "int a;\n" + "}" * n + "\nint after_close() { return 1; }\n")
    write(src / "parens.cpp", "int p = " + "(" * n + "1" + ")" * n + ";\nint after_parens() { return p; }\n")
    write(src / "brackets.cpp", "int q = " + "[" * n + "1" + "]" * n + ";\n")
    ns_open = "".join(f"namespace n{i} {{ " for i in range(n))
    write(src / "namespaces.cpp", ns_open + "int deepest_ns_fn() { return 1; }" + " }" * n + "\n")
    write(src / "namespaces_c++17.cpp", "namespace " + "::".join(f"a{i}" for i in range(n)) + " { int nested_fn() { return 1; } }\n")
    write(src / "templates.cpp",
          "template <typename T> struct Box { T v; };\nusing Deep = " + "Box<" * n + "int" + ">" * n + ";\nint after_templates() { return 1; }\n")
    write(src / "templates_open.cpp", "using Deep = " + "Box<" * n + "int;\nint after_open_templates() { return 1; }\n")
    write(src / "classes.cpp", "".join(f"struct S{i} {{ " for i in range(n)) + "int leaf;" + " };" * n + "\n")
    write(src / "lambdas.cpp", "auto l = " + "[]{ return " * (n // 4) + "1" + "; }()" * (n // 4) + ";\n")
    write(src / "ifdefs.cpp", "".join(f"#if A{i}\n" for i in range(n)) + "int x;\n" + "#endif\n" * n)
    write(src / "ifdefs_unterminated.cpp", "".join(f"#ifdef A{i}\nint v{i};\n" for i in range(n)) + "int tail_after_ifdefs;\n")
    write(src / "comments.cpp", "/* " * n + "text */\nint after_nested_comments() { return 1; }\n")

    c = cfg["ident_chars"]
    big = "Ident" + "_abcdefghij" * (c // 11)
    write(src / "long_ident.cpp", f"int {big} = 1;\nint use_long() {{ return {big}; }}\nstruct {big}_T {{ int {big}_m; }};\n")
    write(src / "long_ident.h", f"#define {big}_MACRO 1\nvoid {big}_fn();\nnamespace {big}_ns {{ void g(); }}\n")

    d = cfg["scope_decls"]
    write_chunks(src / "many_decls.cpp",
                 ("namespace big {\n",
                  "".join(f"int var_{i};\nvoid fn_{i}(int);\nstruct T_{i} {{ int m_{i}; }};\n" for i in range(d)),
                  "}\nint many_decls_tail = big::var_0;\n"))
    write_chunks(src / "many_members.h",
                 ("struct Big {\n", "".join(f"  int member_{i};\n" for i in range(d)), "};\n"))
    write_chunks(src / "many_enums.h",
                 ("enum class E {\n", "".join(f"  Enumerator_{i},\n" for i in range(d)), "};\n"))
    a = cfg["macro_alts"]
    write(src / "macro_alts.cpp",
          "#define ALT_SUM " + " + ".join(f"alt_{i}" for i in range(a)) + "\n"
          "#define ALT_LIST(X) " + " ".join(f"X(alt_{i})" for i in range(a)) + "\n"
          "#define BIG_PARAMS(" + ",".join(f"p{i}" for i in range(a)) + ") p0\n"
          "int alt_macro_use() { return BIG_PARAMS(" + ",".join(str(i) for i in range(a)) + "); }\n"
          "#define PICK(x) (" + " : ".join(f"(x == {i}) ? {i}" for i in range(a)) + " : -1)\n"
          "int pick() { return PICK(3); }\n")
    write(src / "macro_recursive.cpp",
          "#define A B\n#define B C\n#define C A\nint v = A;\n#define F(x) F(x) F(x)\nint w = F(1);\n"
          "#define STR(x) #x\n#define CAT(a,b) a##b\nint CAT(CAT(CAT(a,b),c),d) = 1;\n"
          "#include \"macro_recursive.cpp\"\n")
    write(src / "include_cycle_a.h", '#include "include_cycle_b.h"\nint cyc_a;\n')
    write(src / "include_cycle_b.h", '#include "include_cycle_a.h"\nint cyc_b;\n')
    write(root / "CMakeLists.txt", "# nesting\n")


def sc_encodings(root, cfg, rng):
    e = root / "enc"
    r = random.Random(rng.random())

    def rb(n):
        return bytes(r.getrandbits(8) for _ in range(n))

    base = "int enc_marker() { return 1; }\n// café über 中文\nconst char* s = \"naïve\";\n"
    write(e / "random_binary.cpp", rb(300_000))
    write(e / "random_binary.h", rb(100_000))
    write(e / "random_binary.c", rb(2_000_000))
    write(e / "png_like.cpp", b"\x89PNG\r\n\x1a\n" + rb(50_000))
    write(e / "elf_like.cpp", b"\x7fELF" + rb(50_000))
    write(e / "invalid_utf8.cpp", b"int ok1;\n" + b"\xff\xfe\xfd int bad;\n" + b"\xc3\x28 \xa0\xa1 \xe2\x82 \xf0\x28\x8c\xbc\n" + b"int ok2;\n")
    write(e / "overlong_utf8.cpp", b"int a; // \xc0\xaf \xe0\x80\xaf \xf0\x80\x80\xaf \xed\xa0\x80 \xf4\x90\x80\x80\nint b;\n")
    write(e / "utf16le_bom.cpp", b"\xff\xfe" + base.encode("utf-16-le"))
    write(e / "utf16be_bom.cpp", b"\xfe\xff" + base.encode("utf-16-be"))
    write(e / "utf16le_nobom.cpp", base.encode("utf-16-le"))
    write(e / "utf32_bom.cpp", b"\xff\xfe\x00\x00" + base.encode("utf-32-le"))
    write(e / "utf8_bom.cpp", b"\xef\xbb\xbf" + base.encode("utf-8"))
    write(e / "latin1.cpp", base.encode("latin-1", "replace") + b"// \xe9\xe8\xe0\xfc\n")
    write(e / "cp1252_smart.cpp", b"const char* q = \"\x93smart\x94 \x96 \x85\";\nint cp1252_marker;\n")
    write(e / "nul_bytes.cpp", b"int before_nul;\n\x00\x00\x00\x00 int in_nul;\n" + b"\x00" * 100_000 + b"\nint after_nul;\n")
    write(e / "nul_in_ident.cpp", b"int na\x00me = 1;\nint nul_after;\n")
    write(e / "all_nul.cpp", b"\x00" * 1_000_000)
    write(e / "cr_only.cpp", b"int cr_a;\rint cr_b;\r// comment\rint cr_c;\r" * 2000)
    write(e / "cr_only_comment.cpp", b"// a line comment\rint hidden_in_comment_or_not;\r")
    write(e / "mixed_endings.cpp", b"int m1;\r\nint m2;\nint m3;\rint m4;\r\r\nint m5;\n\n\r\n" * 2000)
    write(e / "crlf_in_raw.cpp", b'const char* r = R"x(a\r\nb\rc\nd)x";\r\nint after_raw;\r\n')
    write(e / "line_continuation.cpp", b"#define M(x) \\\r\n  x + \\\n  x \\\r  + 1\nint after_cont = M(1);\n\\\n\\\n\\")
    write(e / "bom_only.cpp", b"\xef\xbb\xbf")
    write(e / "empty.cpp", b"")
    write(e / "one_byte.cpp", b"{")
    write(e / "trigraphs.cpp", b"??=define X 1\nint a = '??/n';\nint trig_marker;\n")
    write(e / "unicode_idents.cpp", "int äöü = 1; int 中文 = 2; int \U0001F600 = 3; auto λ = [](){};\n".encode("utf-8"))
    write(e / "high_codepoints.cpp", ("const char* s = \"" + "\U0010ffff\u0000".replace("\u0000", "") * 50000 + "\";\n").encode("utf-8"))
    write(e / "zero_width.cpp", "int a​​b = 1; int c﻿d; // ‮ rtl override ⁦\n".encode("utf-8"))
    write(e / "tabs_ff.cpp", b"int\ttab;\x0c\nint\x0bvt;\n" + b"\t" * 50_000 + b"int tab_end;\n")
    write(e / "ctrl_chars.cpp", bytes(range(1, 32)) * 1000 + b"\nint ctrl_marker;\n")
    write(e / "wide_lines.cpp", ("// " + "中" * 100_000 + "\nint wide_marker;\n").encode("utf-8"))
    write(e / "data.txt", rb(500_000))
    write(e / "main.cpp", '#include "latin1.cpp"\nint enc_main() { return enc_marker(); }\n')
    write(root / "CMakeLists.txt", "# encodings\n")


def sc_fsodd(root, cfg, rng):
    w = root / "ws"
    out = root / "outside"
    write(out / "secret.cpp", "int outside_secret = 1;\n")
    write(out / "bigdir" / "x.cpp", "int outside_x;\n")
    write(w / "real" / "a.cpp", "int fs_real_a() { return 1; }\n")
    write(w / "real" / "a.h", "int fs_real_a();\n")
    # loops
    os.makedirs(w / "loop", exist_ok=True)
    os.symlink("..", w / "loop" / "up")  # -> w
    os.symlink("../loop", w / "loop" / "self")
    os.symlink("b", w / "loop" / "a")
    os.symlink("a", w / "loop" / "b")  # a <-> b
    os.symlink("c.cpp", w / "loop" / "c.cpp")  # file linking to itself
    os.symlink("loop2", w / "loop2")  # directory linking to itself
    # outside the workspace
    os.symlink(str(out / "secret.cpp"), w / "link_outside.cpp")
    os.symlink(str(out), w / "link_outside_dir")
    os.symlink("/", w / "link_root")
    os.symlink("/proc/self", w / "link_proc")
    os.symlink("/dev/null", w / "link_devnull.cpp")
    os.symlink("/dev/zero", w / "link_devzero.cpp")
    os.symlink("/dev/urandom", w / "link_urandom.h")
    os.symlink("does/not/exist.cpp", w / "broken.cpp")
    os.symlink("real/a.cpp", w / "link_in.cpp")
    os.symlink("real", w / "link_in_dir")
    os.symlink("../" * 100, w / "link_far_up")
    try:
        os.mkfifo(w / "fifo.cpp")
    except OSError:
        pass
    # permissions
    write(w / "locked" / "inside.cpp", "int locked_inside;\n")
    write(w / "noread.cpp", "int noread_marker;\n")
    write(w / "noread.h", "int noread_h_marker;\n")
    os.chmod(w / "noread.cpp", 0)
    os.chmod(w / "noread.h", 0)
    os.chmod(w / "locked", 0)
    write(w / "noexec" / "inside.cpp", "int noexec_inside;\n")
    os.chmod(w / "noexec", 0o444)  # listable, not traversable
    write(w / "nowrite" / "ro.cpp", "int ro_marker;\n")
    os.chmod(w / "nowrite" / "ro.cpp", 0o444)
    os.chmod(w / "nowrite", 0o555)
    # names
    names = ["spaces in name.cpp", "  leading and trailing  .cpp", "café.cpp", "中文文件.cpp",
             "emoji_\U0001F600.cpp", "tab\tname.cpp", "new\nline.cpp", "quote\"name.cpp", "semi;colon&amp|pipe$dollar`tick`.cpp",
             "-leading-dash.cpp", "--help.cpp", "UPPER.CPP", "noext", "dots...cpp", ".hidden.cpp", "a" * 250 + ".cpp",
             "‮RTL.cpp", "back\\slash.cpp", "percent%20encoded.cpp", "ctrl\x01char.cpp", "nbsp here.cpp"]
    for i, nme in enumerate(names):
        try:
            write(w / "names" / nme, f"int name_marker_{i}() {{ return {i}; }}\n")
        except OSError:
            pass
    for i, d in enumerate(["dir with spaces", "dür", "目录", "dir.cpp", "dir_with.h", "-dash", "x" * 250]):
        write(w / "names" / d / "inner.cpp", f"int inner_marker_{i}() {{ return {i}; }}\n")
    os.makedirs(w / "names" / "empty_dir", exist_ok=True)
    # near PATH_MAX
    target = cfg["path_len"]
    p = str(w / "longpath")
    n = 0
    comp = "p" * 200
    abs_len = len(str(w))
    cur = Path(w) / "longpath"
    pieces = []
    total = abs_len + len("/longpath")
    while total + 201 + 32 < target:
        pieces.append(comp[:200 - len(str(n))] + str(n))
        total += 201
        n += 1
    try:
        # chdir-based creation: single mkdir calls stay under PATH_MAX
        cwd = os.getcwd()
        os.makedirs(cur, exist_ok=True)
        os.chdir(cur)
        try:
            for pc in pieces:
                os.mkdir(pc)
                os.chdir(pc)
            with open("deep_end.cpp", "w") as f:
                f.write("int path_max_marker() { return 1; }\n")
        finally:
            os.chdir(cwd)
    except OSError as ex:
        sys.stderr.write("note: long path stopped early: %s\n" % ex)
        try:
            os.chdir(cwd)
        except OSError:
            pass
    write(w / "CMakeLists.txt", "# fsodd\n")


def sc_truncated(root, cfg, rng):
    t = root / "trunc"
    text = realistic_text(rng, 6)
    n = cfg["trunc_files"]
    for i in range(n):
        cut = rng.randrange(0, len(text))
        ext = ("cpp", "h", "hpp", "cc")[i % 4]
        write(t / f"cut_{i}.{ext}", text[:cut])
    # every file ends mid-construct
    endings = {
        "mid_ident": "int a = 1;\nint some_long_identifi",
        "mid_number": "int a = 0x1F + 1.5e",
        "mid_string": 'const char* s = "unterminated string with \\',
        "mid_string_nl": 'const char* s = "unterminated\nint after_string_nl;\n',
        "mid_char": "char c = '",
        "mid_char2": "char c = '\\",
        "mid_raw_string": 'const char* r = R"delim(raw string never closed\nint inside_raw;\n)delimx" ',
        "mid_raw_prefix": 'const char* r = R"abcdefghijklmnopq',
        "mid_raw_noparen": 'const char* r = R"',
        "mid_line_comment": "int a;\n// comment without newline at eof",
        "mid_line_comment_cont": "int a;\n// continued \\\nstill comment \\",
        "mid_block_comment": "int a;\n/* never closed\nint hidden;\n",
        "mid_block_star": "int a;\n/* almost closed *",
        "mid_preproc": "#define M(x) \\\n  x + \\",
        "mid_include": '#include "never_closed',
        "mid_include_angle": "#include <vector",
        "mid_if": "#if 1\nint a;\n#else\nint b;\n",
        "mid_if_expr": "#if defined(FOO) && (",
        "mid_operator": "int a = 1 +",
        "mid_template": "template <typename T, typename U = std::vector<",
        "mid_class": "class C : public Base {\n public:\n  int m(",
        "mid_namespace": "namespace n { namespace m { struct S { void f() {",
        "mid_lambda": "auto l = [&](int x) -> decltype(",
        "mid_utf8": b"int a; // \xe4\xb8",
        "mid_attr": "[[nodiscard(",
        "mid_extern": 'extern "C',
        "mid_scope": "int x = a::",
        "mid_arrow": "p->",
        "mid_digraph": "<:",
        "mid_ucn": "int \\u00",
        "just_open_brace": "{",
        "just_quote": '"',
        "just_slash": "/",
        "just_hash": "#",
        "just_backslash": "\\",
    }
    for k, v in endings.items():
        write(t / f"end_{k}.cpp", v)
        write(t / f"end_{k}.h", v)
    # a real-looking file whose tail is cut inside each construct kind, with symbols before it
    for k in ("mid_string", "mid_raw_string", "mid_block_comment", "mid_template", "mid_class"):
        v = endings[k]
        v = v if isinstance(v, str) else v.decode("latin-1")
        write(t / f"sym_then_{k}.cpp", "int before_cut_marker() { return 1; }\nstruct BeforeCut {};\n" + v)
    write(t / "main.cpp", "int trunc_main() { return 0; }\n")
    write(root / "CMakeLists.txt", "# truncated\n")


def sc_mixed(root, cfg, rng):
    """A small, valid project: lets the driver verify that queries still answer correctly next to pathological files."""
    write(root / "src" / "shape.h", "#pragma once\nstruct Shape { virtual ~Shape(); virtual double area() const = 0; };\n"
                                    "struct Circle : Shape { double r; double area() const override; };\n")
    write(root / "src" / "shape.cpp", '#include "shape.h"\nShape::~Shape() {}\ndouble Circle::area() const { return 3.14159 * r * r; }\n')
    write(root / "src" / "main.cpp", '#include "shape.h"\nint main() {\n  Circle c;\n  Shape* s = &c;\n  return static_cast<int>(s->area());\n}\n')
    import json
    cc = [{"directory": str(root), "file": str(root / "src" / f), "command": f"c++ -std=c++17 -c {root}/src/{f}"} for f in ("shape.cpp", "main.cpp")]
    write(root / "compile_commands.json", json.dumps(cc))
    sc_truncated_small(root, rng)


def sc_truncated_small(root, rng):
    text = realistic_text(rng, 2)
    for i in range(10):
        write(root / "junk" / f"cut{i}.cpp", text[: rng.randrange(0, len(text))])
    write(root / "junk" / "binary.cpp", bytes(rng.getrandbits(8) for _ in range(50_000)))


SCENARIOS = {
    "tree": sc_tree,
    "bigfiles": sc_bigfiles,
    "nesting": sc_nesting,
    "encodings": sc_encodings,
    "fsodd": sc_fsodd,
    "truncated": sc_truncated,
    "mixed": sc_mixed,
}
# Workspace root (relative to DIR/<scenario>/) that the driver should open; default is the scenario directory.
ROOTS = {"fsodd": "ws"}


def make_scenario(out, name, scale="small", seed=1, overrides=None):
    """Generate one scenario under out/name and return the workspace root path."""
    cfg = Cfg(scale, overrides)
    root = Path(out) / name
    if root.exists():
        for dirpath, dirs, files in os.walk(root):  # undo chmod 000 so rmtree works
            for d in dirs:
                try:
                    os.chmod(os.path.join(dirpath, d), 0o755)
                except OSError:
                    pass
        shutil.rmtree(root, ignore_errors=True)
    root.mkdir(parents=True)
    rng = random.Random(f"{seed}:{name}")
    SCENARIOS[name](root, cfg, rng)
    return root / ROOTS.get(name, "")


# --------------------------------------------------------------------------------------------------------------------
# churn
# --------------------------------------------------------------------------------------------------------------------
def churn(root, seconds, seed, rate=0.0, log=None):
    """Create / modify / rename / delete files and dirs under root until `seconds` pass. Returns the op counts.

    Replaces files by directories and back, renames whole directories, truncates files, grows files, writes
    half-written (non-atomic) content, and removes the directory the editor may be indexing.
    """
    rng = random.Random(f"{seed}:churn")
    root = Path(root)
    counts = {}
    end = time.monotonic() + seconds
    live = []

    def bump(k):
        counts[k] = counts.get(k, 0) + 1

    def pick():
        return rng.choice(live) if live else None

    seqno = 0
    while time.monotonic() < end:
        op = rng.choice(["create", "create", "modify", "append", "rename", "delete", "file2dir", "dir2file", "mkdir",
                         "rmdir", "truncate", "rename_dir", "touch", "partial", "symlink"])
        try:
            if op == "create" or not live:
                seqno += 1
                d = root / "churn" / f"d{rng.randrange(20)}"
                d.mkdir(parents=True, exist_ok=True)
                p = d / f"c{seqno}.{rng.choice(['cpp', 'h', 'txt'])}"
                p.write_text(f"int churn_{seqno}() {{ return {seqno}; }}\n")
                live.append(p)
            elif op == "modify":
                p = pick()
                p.write_text(f"int churn_mod_{rng.randrange(10**6)}() {{ return 1; }}\n" * rng.randrange(1, 50))
            elif op == "append":
                with open(pick(), "a") as f:
                    f.write("int appended_%d;\n" % rng.randrange(10**6))
            elif op == "rename":
                p = pick()
                q = p.with_name(p.stem + "_r" + p.suffix)
                os.rename(p, q)
                live[live.index(p)] = q
            elif op == "delete":
                p = pick()
                live.remove(p)
                os.remove(p)
            elif op == "file2dir":
                p = pick()
                live.remove(p)
                os.remove(p)
                os.mkdir(p)
                (p / "inner.cpp").write_text("int was_a_file;\n")
            elif op == "dir2file":
                ds = [x for x in (root / "churn").iterdir() if x.is_dir()]
                if ds:
                    d = rng.choice(ds)
                    shutil.rmtree(d, ignore_errors=True)
                    d.write_text("int was_a_dir;\n")
                    live[:] = [x for x in live if not str(x).startswith(str(d) + os.sep)]
            elif op == "mkdir":
                (root / "churn" / f"newdir{rng.randrange(50)}" / "a" / "b").mkdir(parents=True, exist_ok=True)
            elif op == "rmdir":
                ds = [x for x in (root / "churn").iterdir() if x.is_dir()]
                if ds:
                    d = rng.choice(ds)
                    shutil.rmtree(d, ignore_errors=True)
                    live[:] = [x for x in live if not str(x).startswith(str(d) + os.sep)]
            elif op == "truncate":
                os.truncate(pick(), rng.randrange(0, 20))
            elif op == "rename_dir":
                ds = [x for x in (root / "churn").iterdir() if x.is_dir()]
                if ds:
                    d = rng.choice(ds)
                    nd = d.with_name(d.name + "_m")
                    os.rename(d, nd)
                    live[:] = [nd / x.relative_to(d) if str(x).startswith(str(d) + os.sep) else x for x in live]
            elif op == "touch":
                os.utime(pick(), None)
            elif op == "partial":
                p = pick()
                with open(p, "w") as f:
                    f.write("int half_writ")
                    f.flush()
                    time.sleep(0.001)
                    f.write("ten() { return 1; }\n")
            elif op == "symlink":
                q = root / "churn" / f"ln{rng.randrange(20)}.cpp"
                if q.is_symlink() or q.exists():
                    q.unlink()
                os.symlink(str(pick() or root), q)
            bump(op)
        except (OSError, ValueError, IndexError):
            bump("err_" + op)
            live[:] = [x for x in live if x.exists()]
        if rate:
            time.sleep(1.0 / rate)
    if log:
        log(counts)
    return counts


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", help="directory to write scenario workspaces into")
    ap.add_argument("--scenario", action="append", help="scenario name (repeatable); default all")
    ap.add_argument("--scale", choices=list(SCALE_INDEX), default="small")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--knob", action="append", help="override a size knob: name=value")
    ap.add_argument("--list", action="store_true", help="list scenarios and knobs")
    ap.add_argument("--churn", metavar="DIR", help="mutate an existing workspace instead of generating")
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--rate", type=float, default=0.0, help="max ops per second (0 = as fast as possible)")
    a = ap.parse_args()
    if a.list:
        for n in SCENARIOS:
            print(n)
        print("knobs (full/asan/small):")
        for k, v in KNOBS.items():
            print("  %-18s %s" % (k, v))
        return 0
    if a.churn:
        print(churn(a.churn, a.seconds, a.seed, a.rate))
        return 0
    if not a.out:
        ap.error("--out is required")
    for n in a.scenario or list(SCENARIOS):
        if n not in SCENARIOS:
            ap.error("unknown scenario %r" % n)
        t = time.monotonic()
        root = make_scenario(a.out, n, a.scale, a.seed, a.knob)
        print("%-10s %s  (%.1fs)" % (n, root, time.monotonic() - t))
    return 0


if __name__ == "__main__":
    sys.exit(main())
