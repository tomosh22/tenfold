#include "ui/widgets/ShellDemo.h"

#include <fstream>

#include "ui/widgets/LinesView.h"

namespace tf::ui {

namespace {
std::shared_ptr<const std::vector<std::string>> readLines(const std::string& path) {
  auto lines = std::make_shared<std::vector<std::string>>();
  std::ifstream in(path, std::ios::binary);
  for (std::string l; std::getline(in, l);) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    lines->push_back(std::move(l));
  }
  if (lines->empty()) lines->push_back("(cannot read " + path + ")");
  return lines;
}
}  // namespace

ShellDemo::ShellDemo(gfx::TextRenderer& text, const std::string& dir, const Theme& theme) : RootWidget(text, theme) {
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
      p->addTab(id++, title(f), std::make_unique<LinesView>(readLines(dir + f)), p == right_ && k == 0);
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
    p->onActivated = [this](int tab) { status_->setText("pos", "Ln 1, Col 1  (tab " + std::to_string(tab) + ")"); };
}

void ShellDemo::onLayout() {
  const RectF& b = bounds();
  const float sh = status_->preferredHeight();
  split_->layout({b.x, b.y, b.w, b.h - sh});
  status_->layout({b.x, b.y + b.h - sh, b.w, sh});
}

}  // namespace tf::ui
