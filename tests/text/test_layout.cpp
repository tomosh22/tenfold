#include "text/text_test_util.h"
#include "ui/gfx/TextLayout.h"

using namespace tftext;

namespace {
struct Rig {
  std::shared_ptr<MockFont> font;
  FontStack stack;
  Atlas atlas;
  TextLayout layout;
  Rig(const std::u32string& cps = U"abcdefghijklmnopqrstuvwxyz ABC^?@中文́�")
      : font(std::make_shared<MockFont>(cps)), atlas(256, 1024), layout((stack.add(font), stack), atlas, 4) {}
  LineLayout run(std::string_view s, float scroll = 0, float vis = 10000, const std::vector<ColorSpan>& sp = {}) {
    LineLayout o;
    layout.layoutLine(s, 100, 50, scroll, vis, sp, 0, o);
    return o;
  }
};
const float kAdv = 8;  // MockFont advance
}  // namespace

TEST(utf8_decode_and_invalid) {
  int len;
  CHECK(decodeUtf8("\xE4\xB8\xAD", "\xE4\xB8\xAD" + 3, len) == U'中');
  CHECK_EQ(len, 3);
  const char bad[] = "\xE4\xB8";  // truncated
  CHECK(decodeUtf8(bad, bad + 2, len) == U'�');
  CHECK_EQ(len, 1);
  const char over[] = "\xC0\x80";  // overlong
  CHECK(decodeUtf8(over, over + 2, len) == U'�');
  const char sur[] = "\xED\xA0\x80";  // surrogate
  CHECK(decodeUtf8(sur, sur + 3, len) == U'�');
  const char lone[] = "\x80";
  CHECK(decodeUtf8(lone, lone + 1, len) == U'�');
}

TEST(layout_basic_positions) {
  Rig r;
  LineLayout o = r.run("ab c");  // space draws nothing
  REQUIRE(o.quads.size() == 3u);
  CHECK_EQ(o.quads[0].x0, 100 + 1);  // origin + bearingX
  CHECK_EQ(o.quads[1].x0, 100 + kAdv + 1);
  CHECK_EQ(o.quads[2].x0, 100 + 3 * kAdv + 1);
  CHECK_EQ(o.quads[0].y0, 50 - 8);
  CHECK_EQ(o.quads[0].y1, 50.0f);
  CHECK(o.quads[0].u1 > o.quads[0].u0 && o.quads[0].u1 <= 1.0f);
  CHECK_EQ(o.runs.size(), 1u);
  CHECK_EQ(o.runs[0].count, 3u);
}

TEST(layout_tabs_expand_to_stops) {
  Rig r;
  std::string s = "a\tb\t\tc";
  CHECK_EQ(r.layout.columnCount(s), 13);  // a, tab to 4, b at 4, tab to 8, tab to 12, c at 12
  LineLayout o = r.run(s);
  REQUIRE(o.quads.size() == 3u);
  CHECK_EQ(o.quads[1].x0, 100 + 4 * kAdv + 1);
  CHECK_EQ(o.quads[2].x0, 100 + 12 * kAdv + 1);
  CHECK_EQ(r.layout.columnForByte(s, 2), 4);
  CHECK_EQ(r.layout.columnForByte(s, 5), 12);
}

TEST(layout_wide_chars_take_two_cells) {
  Rig r;
  std::string s =
      "a\xE4\xB8\xAD\xE6\x96\x87"
      "b";  // a 中 文 b
  CHECK_EQ(r.layout.columnCount(s), 6);
  LineLayout o = r.run(s);
  REQUIRE(o.quads.size() == 4u);
  CHECK_EQ(o.quads[3].x0, 100 + 5 * kAdv + 1);
  CHECK_EQ(r.layout.columnForByte(s, 1), 1);
  CHECK_EQ(r.layout.columnForByte(s, 4), 3);
  CHECK_EQ(r.layout.byteForColumn(s, 2), 1u);  // inside the wide cell snaps to its start
  CHECK_EQ(r.layout.byteForColumn(s, 3), 4u);
}

TEST(layout_combining_marks_overlay_previous_cell) {
  Rig r;
  std::string s =
      "a\xCC\x81"
      "b";  // a + U+0301, b
  CHECK_EQ(r.layout.columnCount(s), 2);
  LineLayout o = r.run(s);
  REQUIRE(o.quads.size() == 3u);
  CHECK_EQ(o.quads[0].x0, o.quads[1].x0);  // mark drawn at base cell origin
  CHECK_EQ(o.quads[2].x0, 100 + kAdv + 1);
  CHECK_EQ(r.layout.columnForByte(s, 1), 0);  // between base and mark: not a caret stop
  CHECK_EQ(r.layout.columnForByte(s, 3), 1);
  CHECK_EQ(r.layout.byteForX(s, 0.9f * kAdv), 3u);  // never lands between base and mark
  CHECK_EQ(r.layout.columnCount("\xCC\x81"), 1);    // orphan mark gets a slot
}

TEST(layout_invalid_utf8_is_one_cell_per_byte) {
  Rig r;
  std::string s =
      "a\xFF\xFE\xE4\xB8"
      "b";  // a, bad, bad, truncated lead (1), continuation byte (1), b
  CHECK_EQ(r.layout.columnCount(s), 6);
  LineLayout o = r.run(s);
  CHECK_EQ(o.quads.size(), 6u);  // each U+FFFD is in the mock font
  CHECK_EQ(r.layout.byteForColumn(s, 4), 4u);
}

TEST(layout_control_chars_show_caret_notation) {
  Rig r;
  std::string s = std::string("a") + '\x01' + "b";
  CHECK_EQ(r.layout.columnCount(s), 4);  // a ^A b
  LineLayout o = r.run(s);
  CHECK_EQ(o.quads.size(), 4u);  // a ^ (A) b
  CHECK_EQ(r.layout.columnCount("\x7f"), 2);
}

TEST(layout_horizontal_scroll_clips_early) {
  Rig r;
  std::string s = "abcdefghijklmnopqrstuvwxyz";
  LineLayout all = r.run(s);
  CHECK_EQ(all.quads.size(), 26u);
  // Window shows x in [5*8, 5*8 + 3*8): columns 5,6,7.
  LineLayout o = r.run(s, 5 * kAdv, 3 * kAdv);
  REQUIRE(o.quads.size() == 3u);
  CHECK_EQ(o.quads[0].x0, 100 + 1 - 5 * kAdv + 5 * kAdv);  // origin + bearing; scroll shifts left by scrollX
  CHECK_EQ(o.quads[0].u0, all.quads[5].u0);
  // A partially visible cell is included.
  LineLayout p = r.run(s, 5 * kAdv + 4, 3 * kAdv);
  CHECK_EQ(p.quads.size(), 4u);
  // Fully scrolled past the end.
  CHECK_EQ(r.run(s, 1000, 100).quads.size(), 0u);
}

TEST(layout_color_spans_split_runs) {
  Rig r;
  std::vector<ColorSpan> sp{{0, 2, 1}, {3, 5, 2}};  // "ab" -> 1, "d","e" -> 2, c and f.. default 0
  LineLayout o = r.run("abcdefg", 0, 10000, sp);
  REQUIRE(o.runs.size() == 4u);
  CHECK_EQ(o.runs[0].color, 1u);
  CHECK_EQ(o.runs[0].count, 2u);
  CHECK_EQ(o.runs[1].color, 0u);
  CHECK_EQ(o.runs[1].count, 1u);
  CHECK_EQ(o.runs[2].color, 2u);
  CHECK_EQ(o.runs[2].count, 2u);
  CHECK_EQ(o.runs[3].color, 0u);
  CHECK_EQ(o.runs[3].firstQuad, 5u);
  // Spans starting before the visible window still apply.
  LineLayout q = r.run("abcdefg", 4 * kAdv, 100, sp);
  REQUIRE(q.runs.size() == 2u);
  CHECK_EQ(q.runs[0].color, 2u);
}

TEST(layout_multiple_lines_append) {
  Rig r;
  LineLayout o;
  r.layout.layoutLine("ab", 0, 10, 0, 1000, {}, 3, o);
  r.layout.layoutLine("cd", 0, 24, 0, 1000, {}, 3, o);
  CHECK_EQ(o.quads.size(), 4u);
  CHECK_EQ(o.runs.size(), 1u);  // runs are colour-only: same colour merges across appended lines
}

TEST(layout_x_column_round_trip_tricky_line) {
  Rig r;
  // tabs, wide, combining, invalid byte, control, plain.
  std::string s =
      "a\tb\xE4\xB8\xAD"
      "c\xCC\x81\t\xFF\x01"
      "z\t\xE6\x96\x87";
  const int total = r.layout.columnCount(s);
  // Every caret stop: bytes that start a cell.
  int stops = 0;
  for (size_t b = 0; b <= s.size(); ++b) {
    float x = r.layout.xForByte(s, b);
    size_t back = r.layout.byteForX(s, x);
    int col = r.layout.columnForByte(s, b);
    CHECK_EQ(x, static_cast<float>(col) * kAdv);
    CHECK_EQ(r.layout.columnForByte(s, back), col);  // x -> byte -> column agrees
    CHECK_EQ(r.layout.byteForColumn(s, col), back);
    if (back == b) ++stops;
  }
  CHECK(stops >= 8);
  // Every column hit-tests to some valid boundary and is monotonic.
  size_t prev = 0;
  for (int c = 0; c <= total; ++c) {
    size_t b = r.layout.byteForX(s, static_cast<float>(c) * kAdv);
    CHECK(b >= prev);
    CHECK(b <= s.size());
    prev = b;
  }
  CHECK_EQ(r.layout.byteForX(s, 99999), s.size());
  CHECK_EQ(r.layout.byteForX(s, -5), 0u);
  // Hit test rounds to the nearer boundary: second half of a 4-wide tab goes to its end.
  std::string t = "a\tb";
  CHECK_EQ(r.layout.byteForX(t, 2.9f * kAdv), 2u);  // col 2.9 in tab spanning cols 1..4 -> nearer 4? start=1 end=4
  CHECK_EQ(r.layout.byteForX(t, 1.4f * kAdv), 1u);
}

TEST(layout_with_real_font_smoke) {
  auto f = loadMono(16);
  if (!f) return;
  FontStack st;
  st.add(f);
  Atlas atlas;
  TextLayout tl(st, atlas, 4);
  LineLayout o;
  tl.layoutLine("int main() { return 0; }", 10, 30, 0, 1000, {}, 0, o);
  CHECK(o.quads.size() >= 18);
  CHECK(!atlas.dirtyRects().empty());
  for (const GlyphQuad& q : o.quads) CHECK(q.u0 >= 0 && q.u1 <= 1 && q.v0 >= 0 && q.v1 <= 1 && q.u1 > q.u0);
  CHECK_EQ(tl.cellWidth(), f->metrics().advance);
}
