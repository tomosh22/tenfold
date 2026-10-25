#include "vcs/LineDiff.h"

#include <algorithm>
#include <unordered_map>

namespace tf {

std::vector<std::string> splitLines(std::string_view text) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (true) {
    size_t nl = text.find('\n', start);
    if (nl == std::string_view::npos) {
      lines.emplace_back(text.substr(start));
      break;
    }
    size_t end = nl;
    if (end > start && text[end - 1] == '\r') --end;
    lines.emplace_back(text.substr(start, end - start));
    start = nl + 1;
  }
  return lines;
}

std::string joinLines(const std::vector<std::string>& lines, size_t first, size_t count) {
  std::string out;
  for (size_t i = 0; i < count; ++i) {
    if (i) out += '\n';
    out += lines[first + i];
  }
  return out;
}

namespace {

HunkKind kindOf(int baseCount, int newCount) {
  if (baseCount == 0) return HunkKind::Added;
  if (newCount == 0) return HunkKind::Deleted;
  return HunkKind::Modified;
}

size_t sz(int i) { return static_cast<size_t>(i); }

// Myers on integer ids a[0..n), b[0..m). Fills delA / insB with the lines that are not part of the common
// subsequence. False when the edit distance exceeds maxD.
bool myers(const std::vector<int>& a, const std::vector<int>& b, int maxD, std::vector<char>& delA,
           std::vector<char>& insB) {
  const int n = static_cast<int>(a.size()), m = static_cast<int>(b.size());
  const int limit = std::min(maxD, n + m);
  const int off = limit + 1;
  std::vector<int> v(sz(2 * limit + 3), 0);
  // trace[d] = V as it was before round d, for k in [-(d-1), d-1] with the parity of d-1 (d values).
  std::vector<std::vector<int>> trace(1);
  int foundD = -1;
  for (int d = 0; d <= limit && foundD < 0; ++d) {
    if (d > 0) {
      std::vector<int> snap(sz(d));
      for (int k = -(d - 1), i = 0; k <= d - 1; k += 2, ++i) snap[sz(i)] = v[sz(off + k)];
      trace.push_back(std::move(snap));
    }
    for (int k = -d; k <= d; k += 2) {
      int x;
      if (d == 0) x = 0;
      else if (k == -d || (k != d && v[sz(off + k - 1)] < v[sz(off + k + 1)])) x = v[sz(off + k + 1)];
      else x = v[sz(off + k - 1)] + 1;
      int y = x - k;
      while (x < n && y < m && a[sz(x)] == b[sz(y)]) {
        ++x;
        ++y;
      }
      v[sz(off + k)] = x;
      if (x >= n && y >= m) {
        foundD = d;
        break;
      }
    }
  }
  if (foundD < 0) return false;
  delA.assign(sz(n), 0);
  insB.assign(sz(m), 0);
  int x = n, y = m;
  for (int d = foundD; d > 0; --d) {
    const std::vector<int>& snap = trace[sz(d)];
    auto at = [&](int k) { return snap[sz((k + d - 1) / 2)]; };
    const int k = x - y;
    const bool down = (k == -d || (k != d && at(k - 1) < at(k + 1)));
    const int prevK = down ? k + 1 : k - 1;
    const int prevX = at(prevK);
    const int prevY = prevX - prevK;
    // The step from (prevX, prevY) is one edit; whatever follows it on the diagonal is common text.
    if (down) insB[sz(prevY)] = 1;  // insertion of b[prevY]
    else delA[sz(prevX)] = 1;       // deletion of a[prevX]
    x = prevX;
    y = prevY;
  }
  return true;
}

}  // namespace

std::vector<DiffHunk> diffLines(const std::vector<std::string>& base, const std::vector<std::string>& cur,
                                const DiffOptions& opt) {
  const int bn = static_cast<int>(base.size()), cn = static_cast<int>(cur.size());
  int pre = 0;
  while (pre < bn && pre < cn && base[sz(pre)] == cur[sz(pre)]) ++pre;
  int suf = 0;
  while (suf < bn - pre && suf < cn - pre && base[sz(bn - 1 - suf)] == cur[sz(cn - 1 - suf)]) ++suf;
  const int an = bn - pre - suf, bm = cn - pre - suf;  // the middle parts
  std::vector<DiffHunk> out;
  if (an == 0 && bm == 0) return out;
  if (an == 0 || bm == 0) {
    out.push_back({pre, an, pre, bm, kindOf(an, bm)});
    return out;
  }
  // Map the middle lines to integer ids so the inner loop compares ints.
  std::unordered_map<std::string_view, int> ids;
  ids.reserve(sz(an + bm));
  std::vector<int> a(sz(an)), b(sz(bm));
  auto idOf = [&](const std::string& s) {
    auto it = ids.try_emplace(std::string_view(s), static_cast<int>(ids.size())).first;
    return it->second;
  };
  for (int i = 0; i < an; ++i) a[sz(i)] = idOf(base[sz(pre + i)]);
  for (int j = 0; j < bm; ++j) b[sz(j)] = idOf(cur[sz(pre + j)]);

  std::vector<char> delA, insB;
  if (!myers(a, b, opt.maxEditDistance, delA, insB)) {
    out.push_back({pre, an, pre, bm, kindOf(an, bm)});
    return out;
  }
  int i = 0, j = 0;
  while (i < an || j < bm) {
    const bool del = i < an && delA[sz(i)];
    const bool ins = j < bm && insB[sz(j)];
    if (!del && !ins) {  // a matched pair
      ++i;
      ++j;
      continue;
    }
    const int i0 = i, j0 = j;
    while (i < an && delA[sz(i)]) ++i;
    while (j < bm && insB[sz(j)]) ++j;
    out.push_back({pre + i0, i - i0, pre + j0, j - j0, kindOf(i - i0, j - j0)});
  }
  return out;
}

std::vector<std::string> hunkBaseLines(const DiffHunk& h, const std::vector<std::string>& base) {
  std::vector<std::string> out;
  for (int i = 0; i < h.baseCount; ++i) {
    size_t at = sz(h.baseStart + i);
    if (at < base.size()) out.push_back(base[at]);
  }
  return out;
}

HunkRevert revertHunkEdit(const DiffHunk& h, const std::vector<std::string>& base,
                          const std::vector<std::string>& cur) {
  const int curN = static_cast<int>(cur.size());
  auto len = [&](int line) { return static_cast<int>(cur[sz(line)].size()); };
  const std::string text = joinLines(base, sz(h.baseStart), sz(h.baseCount));
  HunkRevert r;
  if (h.newCount > 0 && h.baseCount > 0) {  // Modified: swap the lines
    const int last = h.newStart + h.newCount - 1;
    r.range = {{h.newStart, 0}, {last, len(last)}};
    r.text = text;
  } else if (h.newCount > 0) {  // Added: remove the lines together with one line break
    const int last = h.newStart + h.newCount - 1;
    if (last + 1 < curN) r.range = {{h.newStart, 0}, {last + 1, 0}};
    else if (h.newStart > 0) r.range = {{h.newStart - 1, len(h.newStart - 1)}, {last, len(last)}};
    else r.range = {{0, 0}, {last, len(last)}};
  } else if (h.newStart < curN) {  // Deleted: put the lines back in front of the line that now follows
    r.range = {{h.newStart, 0}, {h.newStart, 0}};
    r.text = text + "\n";
  } else {  // Deleted at the very end
    const int last = curN - 1;
    r.range = {{last, len(last)}, {last, len(last)}};
    r.text = "\n" + text;
  }
  return r;
}

}  // namespace tf
