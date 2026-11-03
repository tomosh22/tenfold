// VtScreen: the terminal screen model. Streams are byte strings, several hand-written after what bash, ls and
// full-screen programs emit.
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "term/VtScreen.h"
#include "test_main.h"

using namespace tf;

namespace {

std::string snapshot(const VtScreen& s) {
  std::string out;
  for (int r = 0; r < s.rows(); ++r) {
    for (int c = 0; c < s.cols(); ++c) {
      const VtCell& k = s.cell(r, c);
      vtAppendUtf8(out, k.cp ? k.cp : U'.');
      out += static_cast<char>('a' + (k.attrs & 15));
      out += std::to_string(k.fg) + "," + std::to_string(k.bg) + ";";
    }
    out += '\n';
  }
  out += std::to_string(s.cursorRow()) + "," + std::to_string(s.cursorCol());
  return out;
}

}  // namespace

TEST(vt_plain_text_and_controls) {
  VtScreen s(5, 20);
  s.feed("hello\r\nworld");
  CHECK_EQ(s.rowText(0), "hello");
  CHECK_EQ(s.rowText(1), "world");
  CHECK_EQ(s.cursorRow(), 1);
  CHECK_EQ(s.cursorCol(), 5);
  s.feed("\rWO");  // CR then overwrite
  CHECK_EQ(s.rowText(1), "WOrld");
  s.feed("\bxx");  // BS
  CHECK_EQ(s.rowText(1), "Wxxld");
  s.feed("\r\n\ta\tb");  // default tab stops every 8
  CHECK_EQ(s.rowText(2), "        a       b");
  s.feed("\x07");  // BEL: nothing
  CHECK_EQ(s.cursorRow(), 2);
}

TEST(vt_autowrap_and_pending_wrap) {
  VtScreen s(3, 10);
  s.feed("0123456789");
  CHECK_EQ(s.cursorRow(), 0);  // the wrap is pending, the cursor stays on the last column
  CHECK_EQ(s.cursorCol(), 9);
  s.feed("A");
  CHECK_EQ(s.rowText(0), "0123456789");
  CHECK_EQ(s.rowText(1), "A");
  CHECK(s.row(0).wrapped);
  CHECK(!s.row(1).wrapped);
  // CR after a full line does not wrap
  VtScreen t(3, 10);
  t.feed("0123456789\r\nB");
  CHECK_EQ(t.rowText(1), "B");
  CHECK(!t.row(0).wrapped);
  // DECAWM off: the last column is overwritten
  VtScreen u(3, 10);
  u.feed("\x1b[?7l0123456789XYZ");
  CHECK_EQ(u.rowText(0), "012345678Z");
  CHECK_EQ(u.rowText(1), "");
  CHECK(!u.wrapMode());
}

TEST(vt_scroll_and_scrollback) {
  VtScreen s(4, 10, 100);
  for (int i = 0; i < 10; ++i) s.feed("line" + std::to_string(i) + "\r\n");
  // 10 lines + the empty cursor line: rows hold line7..line9 + empty
  CHECK_EQ(s.rowText(0), "line7");
  CHECK_EQ(s.rowText(2), "line9");
  CHECK_EQ(s.rowText(3), "");
  CHECK_EQ(s.scrollbackSize(), size_t(7));
  std::string first, last;
  for (int c = 0; c < 5; ++c) first += static_cast<char>(s.scrollbackRow(0).cells[static_cast<size_t>(c)].cp);
  for (int c = 0; c < 5; ++c) last += static_cast<char>(s.scrollbackRow(6).cells[static_cast<size_t>(c)].cp);
  CHECK_EQ(first, "line0");
  CHECK_EQ(last, "line6");
}

TEST(vt_scrollback_limit) {
  VtScreen s(2, 8, 5);
  for (int i = 0; i < 50; ++i) s.feed("n" + std::to_string(i) + "\r\n");
  CHECK_EQ(s.scrollbackSize(), size_t(5));
  CHECK_EQ(static_cast<char>(s.scrollbackRow(4).cells[1].cp), '4');  // n48 is the newest scrolled-off line
  CHECK_EQ(static_cast<char>(s.scrollbackRow(4).cells[2].cp), '8');
  s.setScrollbackLimit(2);
  CHECK_EQ(s.scrollbackSize(), size_t(2));
  VtScreen z(2, 8, 0);
  z.feed("a\r\nb\r\nc\r\nd\r\n");
  CHECK_EQ(z.scrollbackSize(), size_t(0));
}

TEST(vt_cursor_movement) {
  VtScreen s(10, 20);
  s.feed("\x1b[5;10H");
  CHECK_EQ(s.cursorRow(), 4);
  CHECK_EQ(s.cursorCol(), 9);
  s.feed("\x1b[2A");
  CHECK_EQ(s.cursorRow(), 2);
  s.feed("\x1b[3B");
  CHECK_EQ(s.cursorRow(), 5);
  s.feed("\x1b[4C");
  CHECK_EQ(s.cursorCol(), 13);
  s.feed("\x1b[20D");
  CHECK_EQ(s.cursorCol(), 0);
  s.feed("\x1b[2E");  // CNL
  CHECK_EQ(s.cursorRow(), 7);
  CHECK_EQ(s.cursorCol(), 0);
  s.feed("\x1b[3F");  // CPL
  CHECK_EQ(s.cursorRow(), 4);
  s.feed("\x1b[8G");  // CHA
  CHECK_EQ(s.cursorCol(), 7);
  s.feed("\x1b[9d");  // VPA
  CHECK_EQ(s.cursorRow(), 8);
  s.feed("\x1b[3;4f");  // HVP
  CHECK_EQ(s.cursorRow(), 2);
  CHECK_EQ(s.cursorCol(), 3);
  s.feed("\x1b[H");
  CHECK_EQ(s.cursorRow(), 0);
  CHECK_EQ(s.cursorCol(), 0);
  s.feed("\x1b[99;99H");  // clamped
  CHECK_EQ(s.cursorRow(), 9);
  CHECK_EQ(s.cursorCol(), 19);
  s.feed("\x1b[0A");  // 0 means 1
  CHECK_EQ(s.cursorRow(), 8);
  s.feed("\x1b[;5H");  // empty first parameter
  CHECK_EQ(s.cursorRow(), 0);
  CHECK_EQ(s.cursorCol(), 4);
}

TEST(vt_save_restore_cursor) {
  VtScreen s(5, 20);
  s.feed(
      "\x1b[3;5H\x1b[31m\x1b"
      "7");
  s.feed("\x1b[1;1H\x1b[0mx");
  s.feed(
      "\x1b"
      "8y");
  CHECK_EQ(s.rowText(2), "    y");
  CHECK_EQ(s.cell(2, 4).fg, vtIndexed(1));
  // CSI s / u
  s.feed("\x1b[5;5H\x1b[s\x1b[1;1H\x1b[u");
  CHECK_EQ(s.cursorRow(), 4);
  CHECK_EQ(s.cursorCol(), 4);
}

TEST(vt_erase) {
  VtScreen s(3, 10);
  s.feed("aaaaaaaaaa\r\nbbbbbbbbbb\r\ncccccccccc");
  s.feed("\x1b[2;5H\x1b[K");  // EL 0
  CHECK_EQ(s.rowText(1), "bbbb");
  s.feed("\x1b[2;3H\x1b[1K");  // EL 1: up to and including the cursor
  CHECK_EQ(s.rowText(1), "   b");
  s.feed("\x1b[3;1H\x1b[2K");
  CHECK_EQ(s.rowText(2), "");
  s.feed("\x1b[1;4H\x1b[J");  // ED 0
  CHECK_EQ(s.rowText(0), "aaa");
  CHECK_EQ(s.rowText(1), "");
  s.feed("\x1b[1;1Haaaaaaaaaa\r\nbbbbbbbbbb\x1b[2;3H\x1b[1J");  // ED 1
  CHECK_EQ(s.rowText(0), "");
  CHECK_EQ(s.rowText(1), "   bbbbbbb");
  s.feed("\x1b[2J");
  CHECK_EQ(s.rowText(1), "");
  s.feed("\x1b[1;1Habcdefghij\x1b[1;3H\x1b[3X");  // ECH
  CHECK_EQ(s.rowText(0), "ab   fghij");
  CHECK_EQ(s.cursorCol(), 2);
}

TEST(vt_erase_uses_current_background) {
  VtScreen s(2, 6);
  s.feed("\x1b[44m\x1b[2J");
  CHECK_EQ(s.cell(1, 5).bg, vtIndexed(4));
  s.feed("\x1b[0m\x1b[1;1H\x1b[K");
  CHECK_EQ(s.cell(0, 0).bg, kVtDefaultColor);
}

TEST(vt_insert_delete_chars_and_lines) {
  VtScreen s(5, 10);
  s.feed("abcdefghij\x1b[1;3H\x1b[2@");
  CHECK_EQ(s.rowText(0), "ab  cdefgh");
  s.feed("\x1b[3P");  // DCH
  CHECK_EQ(s.rowText(0), "abdefgh");
  s.feed("\x1b[1;1H\x1b[100P");
  CHECK_EQ(s.rowText(0), "");
  s.feed("\x1b[1;1H1\r\n2\r\n3\r\n4\r\n5");
  s.feed("\x1b[2;1H\x1b[L");  // IL
  CHECK_EQ(s.rowText(1), "");
  CHECK_EQ(s.rowText(2), "2");
  CHECK_EQ(s.rowText(4), "4");
  s.feed("\x1b[2;1H\x1b[2M");  // DL
  CHECK_EQ(s.rowText(1), "3");
  CHECK_EQ(s.rowText(2), "4");
  CHECK_EQ(s.rowText(4), "");
  s.feed("\x1b[2S");  // SU
  CHECK_EQ(s.rowText(0), "4");
  s.feed("\x1b[1T");  // SD
  CHECK_EQ(s.rowText(0), "");
  CHECK_EQ(s.rowText(1), "4");
  // insert mode
  VtScreen t(2, 10);
  t.feed("abcd\x1b[1;2H\x1b[4hXY\x1b[4l");
  CHECK_EQ(t.rowText(0), "aXYbcd");
}

TEST(vt_scroll_region) {
  VtScreen s(6, 10, 50);
  s.feed("1\r\n2\r\n3\r\n4\r\n5\r\n6");
  s.feed("\x1b[2;4r");  // DECSTBM; moves the cursor home
  CHECK_EQ(s.cursorRow(), 0);
  CHECK_EQ(s.scrollTop(), 1);
  CHECK_EQ(s.scrollBottom(), 3);
  s.feed("\x1b[4;1H\n");  // LF at the bottom margin scrolls the region only
  CHECK_EQ(s.rowText(0), "1");
  CHECK_EQ(s.rowText(1), "3");
  CHECK_EQ(s.rowText(2), "4");
  CHECK_EQ(s.rowText(3), "");
  CHECK_EQ(s.rowText(4), "5");
  CHECK_EQ(s.scrollbackSize(), size_t(0));  // a region not starting at row 0 never feeds the scrollback
  s.feed("\x1b[2;1H\x1bM");                 // RI at the top margin scrolls down
  CHECK_EQ(s.rowText(1), "");
  CHECK_EQ(s.rowText(2), "3");
  s.feed("\x1b[r");  // reset
  CHECK_EQ(s.scrollTop(), 0);
  CHECK_EQ(s.scrollBottom(), 5);
  s.feed("\x1b[5;3r");  // invalid (top >= bottom): ignored
  CHECK_EQ(s.scrollBottom(), 5);
}

TEST(vt_scrollback_only_for_full_screen_region) {
  VtScreen s(4, 10, 50);
  s.feed("\x1b[1;3r\x1b[3;1H\n\n");  // region rows 1-3 (top 0): lines leaving row 0 do go to the scrollback
  CHECK_EQ(s.scrollbackSize(), size_t(2));
}

TEST(vt_sgr_basic_and_extended) {
  VtScreen s(2, 40);
  s.feed("\x1b[1;3;4;7mA\x1b[0mB");
  CHECK_EQ(s.cell(0, 0).attrs, kVtBold | kVtItalic | kVtUnderline | kVtInverse);
  CHECK_EQ(s.cell(0, 1).attrs, 0);
  s.feed("\x1b[31;42mC\x1b[39;49mD\x1b[91;104mE\x1b[mF");
  CHECK_EQ(s.cell(0, 2).fg, vtIndexed(1));
  CHECK_EQ(s.cell(0, 2).bg, vtIndexed(2));
  CHECK_EQ(s.cell(0, 3).fg, kVtDefaultColor);
  CHECK_EQ(s.cell(0, 3).bg, kVtDefaultColor);
  CHECK_EQ(s.cell(0, 4).fg, vtIndexed(9));
  CHECK_EQ(s.cell(0, 4).bg, vtIndexed(12));
  CHECK_EQ(s.cell(0, 5).fg, kVtDefaultColor);
  s.feed("\x1b[38;5;208mG\x1b[48;5;17mH\x1b[38;2;10;20;30mI\x1b[48;2;255;128;0mJ\x1b[0m");
  CHECK_EQ(s.cell(0, 6).fg, vtIndexed(208));
  CHECK_EQ(s.cell(0, 7).bg, vtIndexed(17));
  CHECK_EQ(s.cell(0, 8).fg, vtRgb(10, 20, 30));
  CHECK_EQ(s.cell(0, 9).bg, vtRgb(255, 128, 0));
  CHECK(vtIsRgb(s.cell(0, 8).fg));
  CHECK_EQ(vtGreen(s.cell(0, 9).bg), 128u);
  // extended colour followed by more parameters
  s.feed("\x1b[38;5;1;1;4mK\x1b[0m");
  CHECK_EQ(s.cell(0, 10).fg, vtIndexed(1));
  CHECK_EQ(s.cell(0, 10).attrs, kVtBold | kVtUnderline);
  // colon forms (ISO 8613-6)
  s.feed("\x1b[38:2::1:2:3mL\x1b[38:5:99mM\x1b[4:3mN\x1b[4:0mO\x1b[0m");
  CHECK_EQ(s.cell(0, 11).fg, vtRgb(1, 2, 3));
  CHECK_EQ(s.cell(0, 12).fg, vtIndexed(99));
  CHECK_EQ(s.cell(0, 13).attrs & kVtUnderline, kVtUnderline);
  CHECK_EQ(s.cell(0, 14).attrs & kVtUnderline, 0);
  // reset individual attributes
  s.feed("\x1b[1;2;3;4;7;9m\x1b[22;23;24;27;29mP\x1b[0m");
  CHECK_EQ(s.cell(0, 15).attrs, 0);
  // truncated / malformed extended colour must not corrupt later parameters
  s.feed("\x1b[38;5mQ\x1b[38;2;1mR\x1b[1mS\x1b[0m");
  CHECK_EQ(s.cell(0, 18).attrs, kVtBold);
}

TEST(vt_bash_prompt_with_colours) {
  VtScreen s(10, 60);
  // PS1 of a stock Debian bash: bracketed paste on, window title, green user@host, blue cwd.
  s.feed("\x1b[?2004h\x1b]0;user@host: ~/proj\x07\x1b[01;32muser@host\x1b[00m:\x1b[01;34m~/proj\x1b[00m$ ");
  CHECK(s.bracketedPaste());
  CHECK_EQ(s.title(), "user@host: ~/proj");
  CHECK_EQ(s.rowText(0), "user@host:~/proj$");
  CHECK_EQ(s.cell(0, 0).fg, vtIndexed(2));
  CHECK_EQ(s.cell(0, 0).attrs, kVtBold);
  CHECK_EQ(s.cell(0, 9).fg, kVtDefaultColor);  // ':'
  CHECK_EQ(s.cell(0, 10).fg, vtIndexed(4));
  CHECK_EQ(s.cell(0, 16).fg, kVtDefaultColor);  // '$'
  CHECK_EQ(s.cursorCol(), 18);
  // the user types "ls" and presses Enter: the tty echoes, bash turns bracketed paste off for the command
  s.feed("ls\r\n\x1b[?2004l\r");
  CHECK(!s.bracketedPaste());
  CHECK_EQ(s.cursorRow(), 1);
  CHECK_EQ(s.cursorCol(), 0);
  CHECK_EQ(s.rowText(0), "user@host:~/proj$ ls");
}

TEST(vt_readline_editing_sequences) {
  VtScreen s(3, 40);
  // typing "echo hxllo", moving back and fixing the x as readline does (CUB, ICH-less overwrite, EL)
  s.feed(
      "$ echo hxllo\x1b[4D"
      "e\x1b[3C\x1b[K");
  CHECK_EQ(s.rowText(0), "$ echo hello");
  // backspace-erase as the tty/readline does it: BS, space, BS
  s.feed("\b \b\b \b");
  CHECK_EQ(s.rowText(0), "$ echo hel");
  // readline delete char: ESC [ P
  s.feed("\x1b[2D\x1b[1P");
  CHECK_EQ(s.rowText(0), "$ echo hl");
}

TEST(vt_ls_color_output) {
  VtScreen s(6, 60);
  s.feed(
      "\x1b[0m\x1b[01;34mbuild\x1b[0m  \x1b[01;32mrun.sh\x1b[0m  notes.txt  \x1b[01;31marchive.tar.gz\x1b[0m  "
      "\x1b[01;36mlink\x1b[0m  \x1b[30;42mtmp\x1b[0m\r\n");
  CHECK_EQ(s.rowText(0), "build  run.sh  notes.txt  archive.tar.gz  link  tmp");
  CHECK_EQ(s.cell(0, 0).fg, vtIndexed(4));
  CHECK_EQ(s.cell(0, 0).attrs, kVtBold);
  CHECK_EQ(s.cell(0, 7).fg, vtIndexed(2));
  CHECK_EQ(s.cell(0, 15).fg, kVtDefaultColor);  // notes.txt
  CHECK_EQ(s.cell(0, 26).fg, vtIndexed(1));     // archive.tar.gz
  CHECK_EQ(s.cell(0, 42).fg, vtIndexed(6));     // link
  CHECK_EQ(s.cell(0, 48).fg, vtIndexed(0));     // tmp: black on green
  CHECK_EQ(s.cell(0, 48).bg, vtIndexed(2));
  CHECK_EQ(s.cursorRow(), 1);
  // long listing: -l style columns with escapes at the end of each name (EL is emitted by some ls builds)
  s.feed("drwxr-xr-x 2 u u 4096 Oct 25 build\x1b[K\r\n-rw-r--r-- 1 u u   12 Oct 25 \x1b[0;1;32mrun.sh\x1b[m\x1b[K\r\n");
  CHECK_EQ(s.rowText(2), "-rw-r--r-- 1 u u   12 Oct 25 run.sh");
}

TEST(vt_alt_screen_full_screen_app) {
  VtScreen s(6, 30);
  s.feed("$ vim file\r\n");
  std::string beforeText = s.rowText(0);
  int row = s.cursorRow(), col = s.cursorCol();
  s.feed("\x1b[?1049h\x1b[22;0;0t\x1b[H\x1b[2J\x1b[?25l");
  CHECK(s.altScreen());
  CHECK(!s.cursorVisible());
  CHECK_EQ(s.rowText(0), "");
  s.feed("\x1b[1;1HHELLO\x1b[6;1H\x1b[7m-- INSERT --\x1b[m\x1b[?25h\x1b[1;6H");
  CHECK_EQ(s.rowText(0), "HELLO");
  CHECK_EQ(s.rowText(5), "-- INSERT --");
  CHECK(s.cursorVisible());
  size_t sbBefore = s.scrollbackSize();
  for (int i = 0; i < 20; ++i) s.feed("\x1b[6;1H\n");  // scrolling the alt screen never touches the scrollback
  CHECK_EQ(s.scrollbackSize(), sbBefore);
  s.feed("\x1b[?1049l");
  CHECK(!s.altScreen());
  CHECK_EQ(s.rowText(0), beforeText);
  CHECK_EQ(s.cursorRow(), row);
  CHECK_EQ(s.cursorCol(), col);
  // entering again starts blank
  s.feed("\x1b[?1049h");
  CHECK_EQ(s.rowText(0), "");
  s.feed("\x1b[?1049l");
  // the older modes: 47 keeps the alt content, 1047 clears
  s.feed("\x1b[?47h\x1b[HAAA\x1b[?47l\x1b[?47h");
  CHECK_EQ(s.rowText(0), "AAA");
  s.feed("\x1b[?47l");
  s.feed("\x1b[?1047h\x1b[HBBB\x1b[?1047l\x1b[?1047h");
  CHECK_EQ(s.rowText(0), "");
  s.feed("\x1b[?1047l");
  CHECK(!s.altScreen());
}

TEST(vt_cursor_position_report) {
  VtScreen s(24, 80);
  std::vector<std::string> replies;
  s.setReplyCallback([&](const std::string& r) { replies.push_back(r); });
  s.feed("\x1b[5;10H\x1b[6n");
  REQUIRE(replies.size() == 1);
  CHECK_EQ(replies[0], "\x1b[5;10R");
  s.feed("\x1b[1;1H\x1b[6n");
  CHECK_EQ(replies.back(), "\x1b[1;1R");
  s.feed("\x1b[5n");
  CHECK_EQ(replies.back(), "\x1b[0n");
  s.feed("\x1b[?6n");
  CHECK_EQ(replies.back(), "\x1b[?1;1R");
  s.feed("\x1b[c");
  CHECK_EQ(replies.back(), "\x1b[?1;2c");
  s.feed("\x1b[>c");
  CHECK_EQ(replies.back(), "\x1b[>0;0;0c");
  s.feed("\x1b[18t");
  CHECK_EQ(replies.back(), "\x1b[8;24;80t");
  // origin mode: reported relative to the region
  size_t n = replies.size();
  s.feed("\x1b[5;20r\x1b[?6h\x1b[2;3H\x1b[6n");
  CHECK_EQ(replies.size(), n + 1);
  CHECK_EQ(replies.back(), "\x1b[2;3R");
  CHECK_EQ(s.cursorRow(), 5);  // absolute row 6
  // a split request still answers once
  n = replies.size();
  s.feed("\x1b[");
  s.feed("6");
  s.feed("n");
  CHECK_EQ(replies.size(), n + 1);
  // no callback: no crash
  VtScreen t(5, 5);
  t.feed("\x1b[6n");
}

TEST(vt_title_osc) {
  VtScreen s(3, 20);
  s.feed("\x1b]2;one\x07");
  CHECK_EQ(s.title(), "one");
  s.feed("\x1b]0;two\x1b\\");  // ST terminator
  CHECK_EQ(s.title(), "two");
  s.feed("\x1b]1;icon\x07");  // icon name: ignored
  CHECK_EQ(s.title(), "two");
  s.feed("\x1b]8;;http://x\x1b\\link\x1b]8;;\x1b\\");  // hyperlink: text kept, no title change
  CHECK_EQ(s.rowText(0), "link");
  CHECK_EQ(s.title(), "two");
  s.feed(
      "\x1b]9;9;C:\\dir\x07"
      "x");  // ConPTY cwd report
  CHECK_EQ(s.rowText(0), "linkx");
  std::string big = "\x1b]0;" + std::string(10000, 'T') +
                    "\x07"
                    "y";  // oversize OSC is bounded, parser stays in sync
  s.feed(big);
  CHECK(s.title().size() <= 4096);
  CHECK_EQ(s.rowText(0), "linkxy");
}

TEST(vt_modes) {
  VtScreen s(3, 10);
  CHECK(s.cursorVisible());
  s.feed("\x1b[?25l");
  CHECK(!s.cursorVisible());
  s.feed("\x1b[?25h");
  CHECK(s.cursorVisible());
  s.feed("\x1b[?1h");
  CHECK(s.applicationCursorKeys());
  s.feed("\x1b[?1l");
  CHECK(!s.applicationCursorKeys());
  s.feed("\x1b[?2004h");
  CHECK(s.bracketedPaste());
  s.feed("\x1b[?2004l");
  CHECK(!s.bracketedPaste());
  s.feed("\x1b[?1000h\x1b[?1006h\x1b[?1004h");
  CHECK_EQ(s.mouseTracking(), 1000);
  CHECK(s.mouseSgr());
  CHECK(s.focusReporting());
  s.feed("\x1b[?1000l");
  CHECK_EQ(s.mouseTracking(), 0);
  s.feed("\x1b[2 q");
  CHECK(s.cursorShape() == VtCursorShape::Block);
  s.feed("\x1b=");
  CHECK(s.applicationKeypad());
  s.feed("\x1b>");
  CHECK(!s.applicationKeypad());
  // combined set: ESC [ ? 1 ; 25 l
  s.feed("\x1b[?1h\x1b[?1;25l");
  CHECK(!s.applicationCursorKeys());
  CHECK(!s.cursorVisible());
  // ConPTY start-up noise is accepted
  s.feed("\x1b[?9001h\x1b[?1004h\x1b[?25l\x1b[?3l\x1b[?12l");
  CHECK_EQ(s.rowText(0), "");
}

TEST(vt_utf8_and_wide) {
  VtScreen s(3, 10);
  s.feed("caf\xC3\xA9 \xE4\xB8\xAD\xE6\x96\x87!");
  CHECK_EQ(s.rowText(0), "caf\xC3\xA9 \xE4\xB8\xAD\xE6\x96\x87!");
  CHECK_EQ(static_cast<unsigned>(s.cell(0, 3).cp), static_cast<unsigned>(0xE9));
  CHECK_EQ(s.cell(0, 5).width, 2);
  CHECK_EQ(s.cell(0, 6).width, 0);
  CHECK_EQ(static_cast<unsigned>(s.cell(0, 7).cp), static_cast<unsigned>(0x6587));
  CHECK_EQ(s.cursorCol(), 10 - 1);  // 5 + 4 + 1 = 10 columns used: wrap pending at the last column
  // split across feeds
  VtScreen t(2, 10);
  t.feed("\xE2");
  t.feed("\x82");
  t.feed("\xAC");  // U+20AC
  t.feed("\xF0\x9F");
  t.feed("\x98\x80");  // U+1F600, wide
  CHECK_EQ(static_cast<unsigned>(t.cell(0, 0).cp), static_cast<unsigned>(0x20AC));
  CHECK_EQ(static_cast<unsigned>(t.cell(0, 1).cp), static_cast<unsigned>(0x1F600));
  CHECK_EQ(t.cell(0, 1).width, 2);
  CHECK_EQ(t.cursorCol(), 3);
  // invalid input becomes U+FFFD and the parser resynchronises
  VtScreen u(2, 20);
  u.feed(
      "a\xFF"
      "b\xC3"
      "c\xE2\x82"
      "d\x80"
      "e");
  CHECK_EQ(static_cast<unsigned>(u.cell(0, 0).cp), static_cast<unsigned>('a'));
  CHECK_EQ(static_cast<unsigned>(u.cell(0, 1).cp), static_cast<unsigned>(0xFFFD));
  CHECK_EQ(static_cast<unsigned>(u.cell(0, 2).cp), static_cast<unsigned>('b'));
  CHECK_EQ(static_cast<unsigned>(u.cell(0, 3).cp), static_cast<unsigned>(0xFFFD));
  CHECK_EQ(static_cast<unsigned>(u.cell(0, 4).cp), static_cast<unsigned>('c'));
  CHECK_EQ(static_cast<unsigned>(u.cell(0, 5).cp), static_cast<unsigned>(0xFFFD));
  CHECK_EQ(static_cast<unsigned>(u.cell(0, 6).cp), static_cast<unsigned>('d'));
  CHECK_EQ(static_cast<unsigned>(u.cell(0, 7).cp), static_cast<unsigned>(0xFFFD));
  CHECK_EQ(static_cast<unsigned>(u.cell(0, 8).cp), static_cast<unsigned>('e'));
  // overlong NUL and surrogates
  VtScreen v(2, 10);
  v.feed("\xC0\x80\xED\xA0\x80z");
  CHECK_EQ(v.rowText(0).back(), 'z');
  // combining mark: zero width, dropped
  VtScreen w(2, 10);
  w.feed("e\xCC\x81x");
  CHECK_EQ(static_cast<unsigned>(w.cell(0, 1).cp), static_cast<unsigned>('x'));
}

TEST(vt_wide_char_edges) {
  VtScreen s(3, 5);
  s.feed("abcd\xE4\xB8\xAD");  // the wide char does not fit in the last column: wraps, column 4 stays blank
  CHECK_EQ(static_cast<unsigned>(s.cell(0, 4).cp), static_cast<unsigned>(' '));
  CHECK_EQ(static_cast<unsigned>(s.cell(1, 0).cp), static_cast<unsigned>(0x4E2D));
  // overwriting half of a wide character blanks the other half
  VtScreen t(2, 6);
  t.feed("\xE4\xB8\xAD\x1b[1;2Hx");
  CHECK_EQ(static_cast<unsigned>(t.cell(0, 0).cp), static_cast<unsigned>(' '));
  CHECK_EQ(t.cell(0, 0).width, 1);
  CHECK_EQ(static_cast<unsigned>(t.cell(0, 1).cp), static_cast<unsigned>('x'));
  VtScreen u(2, 6);
  u.feed("\xE4\xB8\xAD\x1b[1;1Hx");
  CHECK_EQ(u.cell(0, 1).width, 1);
  CHECK_EQ(static_cast<unsigned>(u.cell(0, 1).cp), static_cast<unsigned>(' '));
  // erasing the right half
  VtScreen v(2, 6);
  v.feed(
      "a\xE4\xB8\xAD"
      "b\x1b[1;3H\x1b[K");
  CHECK_EQ(static_cast<unsigned>(v.cell(0, 1).cp), static_cast<unsigned>(' '));
  CHECK_EQ(v.cell(0, 1).width, 1);
}

TEST(vt_dec_special_graphics) {
  VtScreen s(2, 10);
  s.feed(
      "\x1b(0lqqk\x1b(B"
      "x\x0e"
      "x\x0f"
      "x");
  CHECK_EQ(static_cast<unsigned>(s.cell(0, 0).cp), static_cast<unsigned>(0x250C));
  CHECK_EQ(static_cast<unsigned>(s.cell(0, 1).cp), static_cast<unsigned>(0x2500));
  CHECK_EQ(static_cast<unsigned>(s.cell(0, 3).cp), static_cast<unsigned>(0x2510));
  CHECK_EQ(static_cast<unsigned>(s.cell(0, 4).cp), static_cast<unsigned>('x'));
  CHECK_EQ(static_cast<unsigned>(s.cell(0, 5).cp), static_cast<unsigned>('x'));  // SO selects G1, which is still ASCII
  s.feed(
      "\x1b)0\x0e"
      "x\x0f"
      "x");
  CHECK_EQ(static_cast<unsigned>(s.cell(0, 7).cp), static_cast<unsigned>(0x2502));
  CHECK_EQ(static_cast<unsigned>(s.cell(0, 8).cp), static_cast<unsigned>('x'));
}

TEST(vt_rep_tabs_reset) {
  VtScreen s(3, 20);
  s.feed("x\x1b[4b");  // REP
  CHECK_EQ(s.rowText(0), "xxxxx");
  s.feed("\r\x1b[3g\x1b[1;5H\x1bH\r\ta");  // clear tab stops, set one at column 5 (0-based 4)
  CHECK_EQ(static_cast<unsigned>(s.cell(0, 4).cp), static_cast<unsigned>('a'));
  s.feed(
      "\x1b[31m\x1b[2;3Hz\x1b"
      "c");  // RIS
  CHECK_EQ(s.rowText(0), "");
  CHECK_EQ(s.rowText(1), "");
  CHECK_EQ(s.cursorRow(), 0);
  CHECK_EQ(s.cursorCol(), 0);
  s.feed("q");
  CHECK_EQ(s.cell(0, 0).fg, kVtDefaultColor);
  // CHT / CBT
  s.feed("\r\x1b[2I");
  CHECK_EQ(s.cursorCol(), 16);
  s.feed("\x1b[Z");
  CHECK_EQ(s.cursorCol(), 8);
}

TEST(vt_unknown_sequences_are_ignored) {
  VtScreen s(3, 40);
  s.feed("a");
  s.feed("\x1b[999z");                // unknown CSI final
  s.feed("\x1b[?9999;1;2$p");         // DECRQM style with intermediate
  s.feed("\x1b[>4;2m");               // xterm modifyOtherKeys
  s.feed("\x1bP1$r0m\x1b\\");         // DCS
  s.feed("\x1b_Gf=24;AAAA\x1b\\");    // APC (kitty graphics)
  s.feed("\x1b^privacy\x1b\\");       // PM
  s.feed("\x1b]777;notify;t;b\x07");  // unknown OSC
  s.feed("\x1b#8");                   // DECALN: ignored
  s.feed("\x1b%G");                   // charset select
  s.feed("\x1b(B\x1b)B\x1b*B\x1b+B");
  s.feed(
      "\x1b[1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;17;18;19;20;21;22;23;24;25;26;27;28;29;30;31;32;33;34;35;36;37;38;"
      "39;40q");
  s.feed("\x1b[<1;2;3M\x1b[=1c");
  s.feed(
      "\x1b[\x18"
      "b");  // CAN aborts the sequence, 'b' is printed
  s.feed(
      "\x1b[1;2\x1b[1;3H"
      "c");  // ESC inside CSI restarts
  CHECK_EQ(s.rowText(0), "abc");
  CHECK_EQ(s.cursorRow(), 0);
  CHECK_EQ(s.cursorCol(), 3);
  s.feed("\x1b[?1049");  // truncated: the next printable would be taken as the final byte; must not crash
  s.feed("h");
  s.feed("\x1b[?1049l");
  CHECK(!s.altScreen());
}

TEST(vt_split_feed_equals_whole_feed) {
  std::string stream =
      "\x1b[?2004h\x1b]0;t\x07\x1b[01;32mhello\x1b[00m \xE4\xB8\xAD\xE6\x96\x87 world\r\n"
      "\x1b[38;2;1;2;3mrgb\x1b[0m\x1b[5;10Hx\x1b[2J\x1b[H\x1b[?1049h\x1b[3;3Halt\x1b[?1049l\x1b[1;"
      "4r\r\n\r\n\r\n\r\n\r\n"
      "\x1b[3L\x1b[2M\x1b[4@\x1b[2P\x1b[1S\x1b[1T\x1b"
      "7\x1b"
      "8\x1b(0lqk\x1b(B\x1b[6n\x1b[999z\x1b]2;title\x1b\\ok";
  VtScreen whole(8, 30);
  whole.feed(stream);
  VtScreen bytewise(8, 30);
  for (char c : stream) bytewise.feed(&c, 1);
  CHECK_EQ(snapshot(whole), snapshot(bytewise));
  CHECK_EQ(whole.title(), bytewise.title());
  CHECK_EQ(whole.scrollbackSize(), bytewise.scrollbackSize());
}

TEST(vt_random_bytes_never_break_invariants) {
  uint32_t seed = 12345;
  auto next = [&]() {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
  };
  static const char* frags[] = {
      "\x1b[", "\x1b]",  "\x1b", ";",   "?",  "1049",       "h",         "l",        "m",
      "J",     "K",      "r",    "H",   "L",  "M",          "@",         "P",        "S",
      "T",     "\r",     "\n",   "\b",  "\t", "38;2;1;2;3", "999999999", "\xE4\xB8", "\xF0\x9F\x98",
      "\x07",  "\x1b\\", ":",    "!",   "p",  "A",          "B",         "C",        "D",
      "X",     "b",      "(0",   "\x18"};
  VtScreen s(7, 23, 30);
  for (int round = 0; round < 3000; ++round) {
    std::string chunk;
    int pieces = 1 + static_cast<int>(next() % 8);
    for (int i = 0; i < pieces; ++i) {
      if (next() % 4 == 0) chunk += static_cast<char>(next() & 0xFF);
      else chunk += frags[next() % (sizeof frags / sizeof frags[0])];
    }
    s.feed(chunk);
    if (round % 500 == 499) s.resize(3 + static_cast<int>(next() % 12), 5 + static_cast<int>(next() % 40));
    REQUIRE(s.cursorRow() >= 0 && s.cursorRow() < s.rows());
    REQUIRE(s.cursorCol() >= 0 && s.cursorCol() < s.cols());
    REQUIRE(s.scrollBottom() < s.rows() && s.scrollTop() <= s.scrollBottom());
    REQUIRE(s.scrollbackSize() <= 30);
  }
  for (int r = 0; r < s.rows(); ++r) {
    CHECK_EQ(static_cast<int>(s.row(r).cells.size()), s.cols());
    for (int c = 0; c < s.cols(); ++c) {
      const VtCell& k = s.cell(r, c);
      if (k.width == 2) {
        REQUIRE(c + 1 < s.cols());
        CHECK_EQ(s.cell(r, c + 1).width, 0);
      }
      if (k.width == 0) {
        REQUIRE(c > 0);
        CHECK_EQ(s.cell(r, c - 1).width, 2);
      }
    }
  }
}

TEST(vt_resize) {
  VtScreen s(5, 10, 20);
  s.feed("one\r\ntwo\r\nthree\r\nfour\r\nfive");
  // narrower: truncated, no reflow
  s.resize(5, 4);
  CHECK_EQ(s.cols(), 4);
  CHECK_EQ(s.rowText(2), "thre");
  CHECK_EQ(s.cursorCol(), 3);
  // wider: padded
  s.resize(5, 12);
  CHECK_EQ(s.rowText(2), "thre");
  CHECK_EQ(static_cast<int>(s.row(2).cells.size()), 12);
  // shorter with the cursor on the last row: the top lines go to the scrollback
  CHECK_EQ(s.cursorRow(), 4);
  s.resize(3, 12);
  CHECK_EQ(s.rows(), 3);
  CHECK_EQ(s.rowText(0), "thre");
  CHECK_EQ(s.rowText(2), "five");
  CHECK_EQ(s.cursorRow(), 2);
  CHECK_EQ(s.scrollbackSize(), size_t(2));
  // taller: blank rows at the bottom
  s.resize(6, 12);
  CHECK_EQ(s.rows(), 6);
  CHECK_EQ(s.rowText(2), "five");
  CHECK_EQ(s.rowText(5), "");
  CHECK_EQ(s.scrollBottom(), 5);
  CHECK(s.allDirty());
  // a wide character cut by the new width is removed
  VtScreen w(2, 6);
  w.feed("ab\xE4\xB8\xAD");
  w.resize(2, 3);
  CHECK_EQ(w.cell(0, 2).width, 1);
  // resize while on the alt screen keeps both buffers consistent
  VtScreen a(4, 10);
  a.feed("main\x1b[?1049h\x1b[Halt");
  a.resize(6, 12);
  CHECK(a.altScreen());
  CHECK_EQ(a.rowText(0), "alt");
  a.feed("\x1b[?1049l");
  CHECK_EQ(a.rowText(0), "main");
  CHECK_EQ(static_cast<int>(a.row(0).cells.size()), 12);
}

TEST(vt_dirty_rows_and_generation) {
  VtScreen s(5, 10);
  CHECK(s.allDirty());
  s.clearDirty();
  CHECK(!s.anyDirty());
  uint64_t g = s.generation();
  s.feed("");
  CHECK_EQ(s.generation(), g);
  s.feed("\x1b[3;1Hx");
  CHECK(s.generation() > g);
  CHECK(s.rowDirty(2));
  CHECK(!s.rowDirty(0));
  CHECK(!s.rowDirty(4));
  s.clearDirty();
  g = s.generation();
  s.feed("\x1b[H");  // cursor move only: the generation changes, no row content did
  CHECK(s.generation() > g);
  CHECK(!s.anyDirty());
  s.clearDirty();
  s.feed(
      "\x1b"
      "M");  // scroll down at the top: the whole region is dirty
  for (int r = 0; r < 5; ++r) CHECK(s.rowDirty(r));
  s.clearDirty();
  g = s.generation();
  s.feed("\x1b]2;new\x07");
  CHECK(s.generation() > g);
  s.clearDirty();
  s.feed("\n\n\n\n\n\n");
  for (int r = 0; r < 5; ++r) CHECK(s.rowDirty(r));
  s.clearDirty();
  s.resize(6, 10);
  CHECK(s.allDirty());
  s.clearDirty();
  CHECK(!s.allDirty());
  s.feed("\x1b[?1049h");
  CHECK(s.allDirty());
}

TEST(vt_clear_scrollback) {
  VtScreen s(2, 5, 20);
  s.feed("a\r\nb\r\nc\r\nd\r\n");
  CHECK(s.scrollbackSize() > 0);
  s.feed("\x1b[3J");
  CHECK_EQ(s.scrollbackSize(), size_t(0));
  CHECK_EQ(s.rowText(0), "d");
}

TEST(vt_cmd_exe_conpty_startup) {
  // What a ConPTY-hosted cmd.exe sends first (abridged): window title, cursor hide, clear, home, banner, prompt.
  VtScreen s(10, 60);
  s.feed(
      "\x1b[?9001h\x1b[?1004h\x1b[?25l\x1b[2J\x1b[m\x1b[H\x1b]0;C:\\WINDOWS\\system32\\cmd.exe\x07\x1b[?25h"
      "Microsoft Windows [Version 10.0.19045.3693]\r\n(c) Microsoft Corporation. All rights reserved.\r\n\r\n"
      "C:\\work>");
  CHECK_EQ(s.rowText(0), "Microsoft Windows [Version 10.0.19045.3693]");
  CHECK_EQ(s.rowText(3), "C:\\work>");
  CHECK_EQ(s.title(), "C:\\WINDOWS\\system32\\cmd.exe");
  CHECK_EQ(s.cursorRow(), 3);
  CHECK_EQ(s.cursorCol(), 8);
  // PowerShell with PSReadLine redraws the line with EL and absolute positioning
  VtScreen p(10, 60);
  p.feed("PS C:\\work> dir\x1b[?25l\x1b[1;17H\x1b[K\x1b[?25h\x1b[1;17H");
  CHECK_EQ(p.rowText(0), "PS C:\\work> dir");
}

TEST(vt_throughput) {
  // ls-like coloured stream. 50 MB (5 MB under sanitizers, which are an order of magnitude slower).
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
  const size_t total = 5u << 20;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
  const size_t total = 5u << 20;
#else
  const size_t total = 50u << 20;
#endif
#else
  const size_t total = 50u << 20;
#endif
  std::string unit;
  const char* names[] = {"Makefile", "README.md",      "src",    "build",         "tests",
                         "docs",     "CMakeLists.txt", "run.sh", "archive.tar.gz"};
  const char* sgrs[] = {"0", "0", "01;34", "01;34", "01;34", "01;34", "0", "01;32", "01;31"};
  for (int line = 0; line < 20; ++line) {
    for (int i = 0; i < 9; ++i) {
      unit += "\x1b[";
      unit += sgrs[i];
      unit += "m";
      unit += names[(i + line) % 9];
      unit += "\x1b[0m  ";
    }
    unit += "\x1b[K\r\n";
  }
  std::string stream;
  while (stream.size() < total) stream += unit;
  VtScreen s(40, 160, 10000);
  auto t0 = std::chrono::steady_clock::now();
  const size_t chunk = 65536;
  for (size_t off = 0; off < stream.size(); off += chunk)
    s.feed(stream.data() + off, std::min(chunk, stream.size() - off));
  double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  double mbs = static_cast<double>(stream.size()) / (1024.0 * 1024.0) / sec;
  std::printf("[term] VtScreen throughput: %.1f MB/s (%zu MB coloured ls-like stream, %.3f s)\n", mbs,
              stream.size() >> 20, sec);
  CHECK(s.scrollbackSize() == 10000);
  CHECK(s.cursorRow() >= 0);
}
