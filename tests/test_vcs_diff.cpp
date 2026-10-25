// C3a: Myers line diff, hunk helpers and the revert-a-hunk edit.
#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "core/TextBuffer.h"
#include "test_main.h"
#include "vcs/LineDiff.h"

using namespace tf;

namespace {

using Lines = std::vector<std::string>;

Lines L(const std::string& text) { return splitLines(text); }

// Length of the longest common subsequence (textbook DP): the reference for minimality.
size_t lcsLength(const Lines& a, const Lines& b) {
  std::vector<std::vector<int>> t(a.size() + 1, std::vector<int>(b.size() + 1, 0));
  for (size_t i = 1; i <= a.size(); ++i)
    for (size_t j = 1; j <= b.size(); ++j)
      t[i][j] = a[i - 1] == b[j - 1] ? t[i - 1][j - 1] + 1 : std::max(t[i - 1][j], t[i][j - 1]);
  return static_cast<size_t>(t[a.size()][b.size()]);
}

// Rebuilds the CURRENT text from the base and the hunks (checks that the hunks describe the change exactly).
Lines apply(const Lines& base, const Lines& cur, const std::vector<DiffHunk>& hs) {
  Lines out;
  int bi = 0;
  for (auto& h : hs) {
    while (bi < h.baseStart) out.push_back(base[static_cast<size_t>(bi++)]);
    for (int k = 0; k < h.newCount; ++k) out.push_back(cur[static_cast<size_t>(h.newStart + k)]);
    bi += h.baseCount;
  }
  while (bi < static_cast<int>(base.size())) out.push_back(base[static_cast<size_t>(bi++)]);
  return out;
}

bool wellFormed(const Lines& base, const Lines& cur, const std::vector<DiffHunk>& hs) {
  int pb = 0, pn = 0;
  for (size_t k = 0; k < hs.size(); ++k) {
    const DiffHunk& h = hs[k];
    if (h.baseCount == 0 && h.newCount == 0) return false;
    if (h.baseStart < pb || h.newStart < pn) return false;
    if (k > 0 && (h.baseStart == pb || h.newStart == pn)) return false;  // touching hunks must have been merged
    // The text between this hunk and the previous one is identical on both sides.
    if (h.baseStart - pb != h.newStart - pn) return false;
    for (int i = 0; i < h.baseStart - pb; ++i)
      if (base[static_cast<size_t>(pb + i)] != cur[static_cast<size_t>(pn + i)]) return false;
    const HunkKind want = h.baseCount == 0 ? HunkKind::Added : h.newCount == 0 ? HunkKind::Deleted : HunkKind::Modified;
    if (h.kind != want) return false;
    pb = h.baseStart + h.baseCount;
    pn = h.newStart + h.newCount;
  }
  return static_cast<int>(base.size()) - pb == static_cast<int>(cur.size()) - pn;
}

// Reverts every hunk (last first, so earlier coordinates stay valid) and returns the resulting lines.
Lines revertAll(const Lines& base, const Lines& cur, const std::vector<DiffHunk>& hs) {
  TextBuffer buf(joinLines(cur, 0, cur.size()));
  for (size_t k = hs.size(); k-- > 0;) {
    HunkRevert r = revertHunkEdit(hs[k], base, cur);
    buf.replace(r.range, r.text);
  }
  return buf.lines();
}

void expectDiff(const std::string& a, const std::string& b, std::vector<DiffHunk> want) {
  Lines base = L(a), cur = L(b);
  auto got = diffLines(base, cur);
  CHECK(got == want);
  CHECK(wellFormed(base, cur, got));
  CHECK(apply(base, cur, got) == cur);
  CHECK(revertAll(base, cur, got) == base);
}

DiffHunk H(int bs, int bc, int ns, int nc) {
  return {bs, bc, ns, nc, bc == 0 ? HunkKind::Added : nc == 0 ? HunkKind::Deleted : HunkKind::Modified};
}

}  // namespace

TEST(linediff_identical_and_empty) {
  expectDiff("", "", {});
  expectDiff("a\nb\nc", "a\nb\nc", {});
  expectDiff("a\nb\n", "a\nb\n", {});
  expectDiff("", "a", {H(0, 1, 0, 1)});     // {""} -> {"a"}
  expectDiff("", "a\n", {H(0, 0, 0, 1)});   // {""} -> {"a",""}: one line added
  expectDiff("a\n", "", {H(0, 1, 0, 0)});   // {"a",""} -> {""}
  expectDiff("a\nb", "", {H(0, 2, 0, 1)});  // everything replaced by the one empty line
}

TEST(linediff_insert_delete_modify_positions) {
  // start
  expectDiff("b\nc\nd", "a\nb\nc\nd", {H(0, 0, 0, 1)});
  expectDiff("a\nb\nc\nd", "b\nc\nd", {H(0, 1, 0, 0)});
  expectDiff("a\nb\nc", "X\nb\nc", {H(0, 1, 0, 1)});
  // middle
  expectDiff("a\nb\nc\nd", "a\nb\nNEW\nc\nd", {H(2, 0, 2, 1)});
  expectDiff("a\nb\nc\nd", "a\nd", {H(1, 2, 1, 0)});
  expectDiff("a\nb\nc\nd", "a\nX\nY\nd", {H(1, 2, 1, 2)});
  expectDiff("a\nb\nc\nd", "a\nX\nd", {H(1, 2, 1, 1)});
  // end
  expectDiff("a\nb\nc", "a\nb\nc\nd\ne", {H(3, 0, 3, 2)});
  expectDiff("a\nb\nc\nd", "a\nb", {H(2, 2, 2, 0)});
  expectDiff("a\nb\nc", "a\nb\nX", {H(2, 1, 2, 1)});
  // two separate hunks
  expectDiff("a\nb\nc\nd\ne", "a\nB\nc\nd\nE\nf", {H(1, 1, 1, 1), H(4, 1, 4, 2)});
  // a repeated line must not confuse the alignment
  expectDiff("x\nx\nx", "x\nx\nx\nx", {H(3, 0, 3, 1)});
}

TEST(linediff_crlf_and_trailing_newline) {
  CHECK(L("a\r\nb\r\n") == (Lines{"a", "b", ""}));
  CHECK(L("a\nb\n") == (Lines{"a", "b", ""}));
  CHECK(L("a\r\nb") == (Lines{"a", "b"}));
  CHECK(L("a\rb\r\n") == (Lines{"a\rb", ""}));  // only a '\r' right before '\n' is a terminator
  // The committed text (LF) against a CRLF buffer, and the other way round, is no change at all.
  CHECK(diffLines(L("one\ntwo\nthree\n"), L("one\r\ntwo\r\nthree\r\n")).empty());
  CHECK(diffLines(L("one\r\ntwo\r\n"), L("one\ntwo\n")).empty());
  // A real change in a CRLF file shows exactly that line.
  expectDiff("one\r\ntwo\r\nthree\r\n", "one\ntwo!\nthree\n", {H(1, 1, 1, 1)});
  // No trailing newline in the base, one in the buffer: the empty last line is new.
  expectDiff("a\nb", "a\nb\n", {H(2, 0, 2, 1)});
  expectDiff("a\nb\n", "a\nb", {H(2, 1, 2, 0)});
  expectDiff("a\nb", "a\nb\nc", {H(2, 0, 2, 1)});
}

TEST(linediff_hunk_base_lines) {
  Lines base = L("a\nb\nc\nd"), cur = L("a\nX\nd");
  auto hs = diffLines(base, cur);
  REQUIRE(hs.size() == 1);
  CHECK(hunkBaseLines(hs[0], base) == (Lines{"b", "c"}));
  Lines base2 = L("a\nb"), cur2 = L("a\nb\nc");
  auto hs2 = diffLines(base2, cur2);
  REQUIRE(hs2.size() == 1);
  CHECK(hunkBaseLines(hs2[0], base2).empty());
}

TEST(linediff_cap_degrades_to_one_hunk) {
  Lines base, cur;
  for (int i = 0; i < 200; ++i) {
    base.push_back("line " + std::to_string(i));
    cur.push_back(i % 2 ? "line " + std::to_string(i) : "changed " + std::to_string(i));
  }
  DiffOptions small;
  small.maxEditDistance = 10;
  auto capped = diffLines(base, cur, small);
  REQUIRE(capped.size() == 1);
  CHECK_EQ(capped[0].baseStart, 0);
  CHECK_EQ(capped[0].newStart, 0);
  CHECK(wellFormed(base, cur, capped));
  CHECK(apply(base, cur, capped) == cur);
  CHECK(revertAll(base, cur, capped) == base);
  // The cap applies to the part between the common prefix and suffix only.
  base.insert(base.begin(), 100, "same");
  cur.insert(cur.begin(), 100, "same");
  auto capped2 = diffLines(base, cur, small);
  REQUIRE(capped2.size() == 1);
  CHECK_EQ(capped2[0].baseStart, 100);
  // Uncapped it is the exact, minimal diff.
  auto exact = diffLines(base, cur);
  CHECK_EQ(exact.size(), static_cast<size_t>(100));
  CHECK(wellFormed(base, cur, exact));
}

TEST(linediff_random_property_vs_lcs) {
  std::mt19937 rng(12345);
  for (int iter = 0; iter < 4000; ++iter) {
    const int alphabet = 2 + static_cast<int>(rng() % 4);
    auto gen = [&](int maxLen) {
      Lines v(rng() % static_cast<unsigned>(maxLen + 1));
      for (auto& s : v) s = std::string(1, static_cast<char>('a' + rng() % static_cast<unsigned>(alphabet)));
      if (v.empty()) v.push_back("");  // buffers always have a line
      return v;
    };
    Lines base = gen(14), cur;
    if (rng() % 2) {  // related texts: a few random edits of the base
      cur = base;
      for (int e = static_cast<int>(rng() % 4); e > 0; --e) {
        size_t at = rng() % (cur.size() + 1);
        switch (rng() % 3) {
          case 0:
            cur.insert(cur.begin() + static_cast<long>(at), std::string(1, static_cast<char>('a' + rng() % 5)));
            break;
          case 1:
            if (at < cur.size()) cur.erase(cur.begin() + static_cast<long>(at));
            break;
          default:
            if (at < cur.size()) cur[at] = "Z";
        }
      }
      if (cur.empty()) cur.push_back("");
    } else {
      cur = gen(14);
    }
    auto hs = diffLines(base, cur);
    if (!wellFormed(base, cur, hs) || apply(base, cur, hs) != cur || revertAll(base, cur, hs) != base) {
      CHECK(false);
      std::cerr << "    failing iteration " << iter << "\n";
      return;
    }
    size_t edits = 0;
    for (auto& h : hs) edits += static_cast<size_t>(h.baseCount + h.newCount);
    const size_t lcs = lcsLength(base, cur);
    if (edits != base.size() + cur.size() - 2 * lcs) {
      CHECK_EQ(edits, base.size() + cur.size() - 2 * lcs);
      std::cerr << "    not minimal at iteration " << iter << "\n";
      return;
    }
  }
}

TEST(linediff_random_large_validity) {
  std::mt19937 rng(777);
  Lines base;
  for (int i = 0; i < 3000; ++i) base.push_back("row " + std::to_string(rng() % 500));  // plenty of repeats
  for (int round = 0; round < 8; ++round) {
    Lines cur = base;
    for (int e = 0; e < 60; ++e) {
      size_t at = rng() % cur.size();
      switch (rng() % 3) {
        case 0: cur.insert(cur.begin() + static_cast<long>(at), "new " + std::to_string(e)); break;
        case 1: cur.erase(cur.begin() + static_cast<long>(at)); break;
        default: cur[at] = "edit " + std::to_string(e);
      }
    }
    auto hs = diffLines(base, cur);
    CHECK(wellFormed(base, cur, hs));
    CHECK(apply(base, cur, hs) == cur);
    CHECK(revertAll(base, cur, hs) == base);
    CHECK(hs.size() <= 60);
  }
}

TEST(linediff_revert_hunk_edit_cases) {
  // Each hunk kind at the start, middle and end of the document, reverted one at a time on the text.
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"a\nb\nc", "X\nb\nc"},
      {"a\nb\nc", "a\nX\nc"},
      {"a\nb\nc", "a\nb\nX"},
      {"a\nb\nc", "NEW\na\nb\nc"},
      {"a\nb\nc", "a\nNEW\nb\nc"},
      {"a\nb\nc", "a\nb\nc\nNEW"},
      {"a\nb\nc", "b\nc"},
      {"a\nb\nc", "a\nc"},
      {"a\nb\nc", "a\nb"},
      {"a", ""},
      {"", "x\ny"},
      {"a\nb\n", "a\nb\nc\n"},
      {"a\nb\n", "a\n"},
      {"a\nb", "b"},
  };
  for (auto& [a, b] : cases) {
    Lines base = L(a), cur = L(b);
    auto hs = diffLines(base, cur);
    CHECK(revertAll(base, cur, hs) == base);
    // single hunk reverted on its own (the others stay as edited)
    for (size_t k = 0; k < hs.size(); ++k) {
      TextBuffer buf(b);
      HunkRevert r = revertHunkEdit(hs[k], base, cur);
      buf.replace(r.range, r.text);
      auto after = diffLines(base, buf.lines());
      CHECK_EQ(after.size(), hs.size() - 1);
    }
  }
}

// ---------------------------------------------------------------------------
// Benchmark (Release numbers go into the task report; nothing here asserts on time)
// ---------------------------------------------------------------------------

namespace {
#ifdef NDEBUG
constexpr int kBenchRuns = 5;
#else
constexpr int kBenchRuns = 1;  // sanitizer builds: just prove it runs
#endif
double medianMs(int runs, const std::function<void()>& fn) {
  std::vector<double> t;
  for (int i = 0; i < runs; ++i) {
    auto t0 = std::chrono::steady_clock::now();
    fn();
    t.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
  }
  std::sort(t.begin(), t.end());
  return t[t.size() / 2];
}
}  // namespace

TEST(linediff_bench_100k_lines) {
  std::mt19937 rng(99);
  Lines base;
  base.reserve(100000);
  for (int i = 0; i < 100000; ++i)
    base.push_back("    int value_" + std::to_string(i) + " = compute(" + std::to_string(rng() % 100000) + ");");

  Lines one = base;
  one[50000] = "    int changed = 0;";
  std::vector<DiffHunk> h1;
  const double ms1 = medianMs(kBenchRuns, [&] { h1 = diffLines(base, one); });
  CHECK_EQ(h1.size(), static_cast<size_t>(1));

  Lines pct = base;  // 1% of the lines modified, spread out
  for (size_t i = 50; i < pct.size(); i += 100) pct[i] = "    // edited " + std::to_string(i);
  std::vector<DiffHunk> h2;
  const double ms2 = medianMs(kBenchRuns, [&] { h2 = diffLines(base, pct); });
  CHECK_EQ(h2.size(), static_cast<size_t>(1000));
  CHECK(wellFormed(base, pct, h2));

  Lines inserted = base;  // 1% of the lines inserted
  for (size_t i = 1000; i > 0; --i) inserted.insert(inserted.begin() + static_cast<long>(i * 99), "    // added");
  std::vector<DiffHunk> h3;
  const double ms3 = medianMs(kBenchRuns, [&] { h3 = diffLines(base, inserted); });
  CHECK_EQ(h3.size(), static_cast<size_t>(1000));

  // What the main thread pays per diff: the snapshot copy of the buffer lines.
  double copyMs = medianMs(kBenchRuns, [&] {
    Lines c = pct;
    if (c.size() != pct.size()) std::abort();
  });
  std::cout << "BENCH linediff 100k lines: 1 changed line " << ms1 << " ms, 1% modified (1000 hunks) " << ms2
            << " ms, 1% inserted " << ms3 << " ms; snapshot copy on the main thread " << copyMs << " ms\n";
}
