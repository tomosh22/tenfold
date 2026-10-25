#include "ui/widgets/ShellDemo.h"

namespace tf::ui {

TextView* ShellDemo::view(int tabId) const {
  auto it = views_.find(tabId);
  return it == views_.end() ? nullptr : it->second;
}

ShellDemo::ShellDemo(gfx::TextRenderer& text, const std::string& dir, const Theme& theme)
    : RootWidget(text, theme), editor_(std::make_unique<Editor>(dir)) {
  auto split = std::make_unique<Splitter>(Splitter::Orientation::Horizontal);
  auto left = std::make_unique<Panel>(), right = std::make_unique<Panel>();
  left_ = left.get();
  right_ = right.get();
  const char* files[] = {"/src/main.cpp", "/src/geometry.cpp", "/include/geometry.h"};
  auto title = [](const std::string& p) { return p.substr(p.rfind('/') + 1); };
  int id = 1;
  for (Panel* p : {left_, right_}) {
    for (int k = 0; k < 3; ++k) {
      const std::string f = files[p == left_ ? k : 2 - k];
      auto tv = std::make_unique<TextView>(*editor_, editor_->open(dir + f));
      views_[id] = tv.get();
      tv->onStateChanged = [this, v = tv.get()] {
        if (v->hasFocus()) status_->setText("pos", v->positionText());
      };
      p->addTab(id++, title(f), std::move(tv), p == right_ && k == 0);
    }
    p->setActive(p == left_ ? 1 : 5);
  }
  left_->tabs().setDirty(2, true);
  split->setChildren(std::move(left), std::move(right));
  split_ = static_cast<Splitter*>(addChild(std::move(split)));

  auto sb = std::make_unique<StatusBar>();
  status_ = sb.get();
  addChild(std::move(sb));
  status_->setText("workspace", "sample_project");
  status_->setText("pos", "Ln 1, Col 1");
  status_->setText("clangd", "clangd: indexing 43%");
  status_->setText("fastindex", "fast index: ready");
  status_->addSegment("encoding", StatusBar::Align::Right);
  status_->addSegment("eol", StatusBar::Align::Right,
                      [this](const std::string& k) { status_->setText(k, status_->text(k) == "LF" ? "CRLF" : "LF"); });
  status_->setText("encoding", "UTF-8");
  status_->setText("eol", "LF");
  for (Panel* p : {left_, right_})
    p->onActivated = [this](int tab) {
      if (TextView* v = view(tab)) {
        v->activate();
        status_->setText("pos", v->positionText());
      }
    };
  if (TextView* v = view(left_->activeId())) v->activate();
}

void ShellDemo::onLayout() {
  const RectF& b = bounds();
  const float sh = status_->preferredHeight();
  split_->layout({b.x, b.y, b.w, b.h - sh});
  status_->layout({b.x, b.y + b.h - sh, b.w, sh});
}

}  // namespace tf::ui
