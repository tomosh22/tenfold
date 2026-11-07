// DropDown and CheckBox (src/ui/widgets): layout, keyboard and mouse, in a RootWidget with a recording renderer and
// the mock font (8 px cells, 14 px lines).
#include <vector>

#include "../text/text_test_util.h"
#include "ui/gfx/TextRenderer.h"
#include "ui/widgets/CheckBox.h"
#include "ui/widgets/DropDown.h"
#include "ui_test.h"

using namespace tf;
using namespace tf::ui;

namespace {

struct NullRenderer : gfx::Renderer {
  int rects = 0;
  void beginFrame(int, int, gfx::Color) override { rects = 0; }
  void drawRect(const gfx::RectF&, gfx::Color) override { ++rects; }
  void drawLine(float, float, float, float, float, gfx::Color) override {}
  void drawGlyphs(gfx::TextureId, const gfx::GlyphQuad*, int, gfx::Color) override {}
  void pushScissor(const gfx::RectF&) override {}
  void popScissor() override {}
  gfx::TextureId createTexture(int, int, gfx::TextureFormat) override { return 1; }
  void updateTexture(gfx::TextureId, int, int, int, int, const void*, int) override {}
  void destroyTexture(gfx::TextureId) override {}
  void endFrame() override {}
  bool readPixels(int, int, uint8_t*) override { return false; }
};

struct Fixture {
  std::unique_ptr<gfx::TextRenderer> text;
  std::unique_ptr<RootWidget> root;
  NullRenderer rec;
  DropDown* dd = nullptr;
  CheckBox* cb = nullptr;
  DropDown* low = nullptr;  // near the bottom edge: its list opens upwards
  std::vector<int> changes;
  std::vector<bool> ticks;

  Fixture() {
    gfx::FontStack stack;
    stack.add(std::make_shared<tftext::MockFont>(U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ()"));
    text = gfx::TextRenderer::fromFonts(std::move(stack));
    root = std::make_unique<RootWidget>(*text);
    auto d = std::make_unique<DropDown>();
    dd = static_cast<DropDown*>(root->addChild(std::move(d)));
    dd->setItems({{"Windows (CR LF)", true}, {"Macintosh (CR)", true}, {"Unix (LF)", true}, {"Disabled one", false}, {"Last", true}},
                 2);
    dd->onChanged([this](int i) { changes.push_back(i); });
    auto c = std::make_unique<CheckBox>();
    cb = static_cast<CheckBox*>(root->addChild(std::move(c)));
    cb->setLabel("Do not show this message again");
    cb->onChanged([this](bool on) { ticks.push_back(on); });
    auto l = std::make_unique<DropDown>();
    low = static_cast<DropDown*>(root->addChild(std::move(l)));
    low->setItems({{"a", true}, {"b", true}, {"c", true}}, 0);
    render();
    dd->layout({20, 20, 200, dd->preferredHeight()});
    cb->layout({20, 250, cb->preferredWidth(), cb->preferredHeight()});
    low->layout({20, 380, 120, low->preferredHeight()});
  }
  void render() { root->render(rec, 400, 400); }
  bool send(const InputEvent& e) { return root->dispatch(e); }
  bool key(Key k, uint8_t mods = 0) {
    InputEvent e;
    e.type = InputType::KeyDown;
    e.key = k;
    e.mods = mods;
    return send(e);
  }
  void mouse(InputType t, float x, float y) {
    InputEvent e;
    e.type = t;
    e.x = x;
    e.y = y;
    e.clicks = 1;
    send(e);
  }
  void click(float x, float y) {
    mouse(InputType::MouseDown, x, y);
    mouse(InputType::MouseUp, x, y);
  }
  void click(const RectF& r) { click(r.x + r.w / 2, r.y + r.h / 2); }
};

}  // namespace

TEST(dropdown_layout_box_and_list) {
  Fixture f;
  CHECK_EQ(f.dd->selected(), 2);
  CHECK_EQ(f.dd->selectedLabel(), std::string("Unix (LF)"));
  CHECK(f.dd->preferredWidth() > f.text->measure("Windows (CR LF)"));
  CHECK(f.dd->preferredHeight() >= f.text->lineHeight());
  CHECK(!f.dd->isOpen());
  CHECK_EQ(f.dd->listRect().h, 0.0f);
  f.dd->setFocus();
  f.dd->open();
  f.render();
  const RectF b = f.dd->bounds(), l = f.dd->listRect();
  CHECK(l.y >= b.y + b.h - 1);  // opens below the box
  CHECK_EQ(l.x, b.x);
  CHECK_EQ(l.w, b.w);
  CHECK(f.dd->itemRect(0).y < f.dd->itemRect(1).y);
  CHECK(f.dd->itemRect(4).y + f.dd->itemRect(4).h <= l.y + l.h);
  // the list takes mouse events beyond the bounds
  CHECK(f.dd->hitRect().h > b.h);
  CHECK(f.root->hitTest(l.x + 5, l.y + l.h - 3) == f.dd);
  f.dd->closeList();
  CHECK(f.root->hitTest(l.x + 5, l.y + l.h - 3) != f.dd);
}

TEST(dropdown_list_opens_upwards_near_the_window_bottom) {
  Fixture f;
  f.low->setFocus();
  f.low->open();
  f.render();
  CHECK(f.low->listRect().y + f.low->listRect().h <= f.low->bounds().y + 1);
  CHECK(f.low->listRect().y >= 0);
}

TEST(dropdown_keyboard_closed) {
  Fixture f;
  f.dd->setFocus();
  CHECK(f.key(Key::Up));
  CHECK_EQ(f.dd->selected(), 1);
  CHECK(f.key(Key::Home));
  CHECK_EQ(f.dd->selected(), 0);
  CHECK(f.key(Key::Up));  // already first
  CHECK_EQ(f.dd->selected(), 0);
  CHECK(f.key(Key::Down));
  CHECK(f.key(Key::Down));
  CHECK_EQ(f.dd->selected(), 2);
  CHECK(f.key(Key::Down));  // skips the disabled entry
  CHECK_EQ(f.dd->selected(), 4);
  CHECK(f.key(Key::Down));
  CHECK_EQ(f.dd->selected(), 4);
  CHECK(f.key(Key::End));
  CHECK(f.changes == (std::vector<int>{1, 0, 1, 2, 4}));
  CHECK(!f.dd->isOpen());
}

TEST(dropdown_keyboard_open_choose_and_cancel) {
  Fixture f;
  f.dd->setFocus();
  CHECK(f.key(Key::Down, ModAlt));  // Alt+Down opens
  CHECK(f.dd->isOpen());
  CHECK_EQ(f.dd->highlighted(), 2);
  CHECK(f.key(Key::Up));
  CHECK(f.key(Key::Up));
  CHECK(f.key(Key::Up));  // stays on the first
  CHECK_EQ(f.dd->highlighted(), 0);
  CHECK_EQ(f.dd->selected(), 2);  // moving the highlight chooses nothing
  CHECK(f.key(Key::End));
  CHECK_EQ(f.dd->highlighted(), 4);
  CHECK(f.key(Key::Up));  // skips the disabled entry
  CHECK_EQ(f.dd->highlighted(), 2);
  CHECK(f.key(Key::Up));
  CHECK(f.key(Key::Enter));  // chooses Macintosh and closes
  CHECK(!f.dd->isOpen());
  CHECK_EQ(f.dd->selected(), 1);
  CHECK(f.changes == (std::vector<int>{1}));
  // Enter opens (default), Escape closes without choosing and is consumed
  CHECK(f.key(Key::Enter));
  CHECK(f.dd->isOpen());
  CHECK(f.key(Key::Down));
  CHECK(f.key(Key::Escape));
  CHECK(!f.dd->isOpen());
  CHECK_EQ(f.dd->selected(), 1);
  CHECK_EQ(f.changes.size(), size_t(1));
  // Space opens and chooses; F4 opens; Alt+Up chooses the highlight; choosing the same entry fires nothing
  CHECK(f.key(Key::Space));
  CHECK(f.dd->isOpen());
  CHECK(f.key(Key::Space));
  CHECK(!f.dd->isOpen());
  CHECK_EQ(f.changes.size(), size_t(1));
  CHECK(f.key(Key::F4));
  CHECK(f.dd->isOpen());
  CHECK(f.key(Key::Down));
  CHECK(f.key(Key::Up, ModAlt));
  CHECK(!f.dd->isOpen());
  CHECK_EQ(f.dd->selected(), 2);
}

TEST(dropdown_enter_bubbles_when_it_does_not_open) {
  Fixture f;
  f.dd->setEnterOpens(false);
  f.dd->setFocus();
  CHECK(!f.key(Key::Enter));
  CHECK(!f.dd->isOpen());
  CHECK(f.key(Key::F4));  // other ways to open remain
  CHECK(f.dd->isOpen());
  CHECK(f.key(Key::Enter));  // an open list takes Enter
  CHECK(!f.dd->isOpen());
}

TEST(dropdown_tab_closes_the_list_and_bubbles) {
  Fixture f;
  f.dd->setFocus();
  f.dd->open();
  CHECK(!f.key(Key::Tab));
  CHECK(!f.dd->isOpen());
}

TEST(dropdown_mouse) {
  Fixture f;
  f.click(f.dd->bounds());
  CHECK(f.dd->isOpen());
  CHECK(f.dd->hasFocus());
  f.render();
  f.mouse(InputType::MouseMove, f.dd->itemRect(0).x + 3, f.dd->itemRect(0).y + 3);
  CHECK_EQ(f.dd->highlighted(), 0);
  f.mouse(InputType::MouseMove, f.dd->itemRect(3).x + 3, f.dd->itemRect(3).y + 3);
  CHECK_EQ(f.dd->highlighted(), 0);  // a disabled entry is not highlighted
  f.click(f.dd->itemRect(3));        // ... and not chosen
  CHECK(f.dd->isOpen());
  CHECK_EQ(f.dd->selected(), 2);
  f.click(f.dd->itemRect(4));
  CHECK(!f.dd->isOpen());
  CHECK_EQ(f.dd->selected(), 4);
  CHECK(f.changes == (std::vector<int>{4}));
  // a click on the box toggles
  f.click(f.dd->bounds());
  CHECK(f.dd->isOpen());
  f.click(f.dd->bounds());
  CHECK(!f.dd->isOpen());
  // a click on the item that is already chosen closes without a change
  f.click(f.dd->bounds());
  f.click(f.dd->itemRect(4));
  CHECK(!f.dd->isOpen());
  CHECK_EQ(f.changes.size(), size_t(1));
  // losing the focus closes the list
  f.click(f.dd->bounds());
  CHECK(f.dd->isOpen());
  f.cb->setFocus();
  CHECK(!f.dd->isOpen());
  // a right click does nothing
  InputEvent e;
  e.type = InputType::MouseDown;
  e.button = MouseButton::Right;
  e.x = f.dd->bounds().x + 3;
  e.y = f.dd->bounds().y + 3;
  f.send(e);
  CHECK(!f.dd->isOpen());
}

TEST(dropdown_programmatic_selection_and_items) {
  Fixture f;
  f.dd->setSelected(0);
  CHECK_EQ(f.dd->selected(), 0);
  f.dd->setSelected(3);  // disabled: ignored
  f.dd->setSelected(9);
  CHECK_EQ(f.dd->selected(), 0);
  CHECK(f.changes.empty());  // onChanged is for user choices
  f.dd->setEnabled(3, true);
  f.dd->setSelected(3);
  CHECK_EQ(f.dd->selectedLabel(), std::string("Disabled one"));
  f.dd->setItems({{"only", true}}, 5);
  CHECK_EQ(f.dd->selected(), 0);
  f.dd->setItems({}, 0);
  CHECK_EQ(f.dd->selectedLabel(), std::string());
  f.dd->open();  // nothing to show
  CHECK(!f.dd->isOpen());
}

TEST(checkbox_layout_and_label) {
  Fixture f;
  f.render();
  CHECK(!f.cb->checked());
  CHECK(f.cb->preferredWidth() > f.text->measure("Do not show this message again"));
  const RectF box = f.cb->boxRect(), b = f.cb->bounds();
  CHECK(box.w > 0 && box.w == box.h);
  CHECK(box.x >= b.x && box.x + box.w < b.x + b.w);
  CHECK(box.y >= b.y && box.y + box.h <= b.y + b.h);
}

TEST(checkbox_keyboard_space_toggles) {
  Fixture f;
  f.cb->setFocus();
  CHECK(f.key(Key::Space));
  CHECK(f.cb->checked());
  CHECK(f.key(Key::Space));
  CHECK(!f.cb->checked());
  CHECK(!f.key(Key::Enter));  // Enter is not handled: a dialog's default button gets it
  CHECK(!f.key(Key::Space, ModCtrl));
  CHECK(f.ticks == (std::vector<bool>{true, false}));
}

TEST(checkbox_mouse_box_and_label_toggle_and_focus) {
  Fixture f;
  f.click(f.cb->boxRect());
  CHECK(f.cb->checked());
  CHECK(f.cb->hasFocus());
  f.click(f.cb->bounds().x + f.cb->bounds().w - 4, f.cb->bounds().y + 4);  // on the label
  CHECK(!f.cb->checked());
  f.click(f.cb->bounds().x + f.cb->bounds().w + 20, f.cb->bounds().y + 4);  // outside
  CHECK(!f.cb->checked());
  f.cb->setChecked(true);  // programmatic: no callback
  CHECK(f.ticks == (std::vector<bool>{true, false}));
  f.cb->toggle();
  CHECK(!f.cb->checked());
  CHECK_EQ(f.ticks.size(), size_t(3));
}
