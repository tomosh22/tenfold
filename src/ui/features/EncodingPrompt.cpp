#include "ui/features/EncodingPrompt.h"

#include "ui/features/LineEndingPrompt.h"

namespace tf::ui {

namespace fs = std::filesystem;

namespace {
// VS's order: Windows, Macintosh, Unix, then the Unicode separators.
constexpr LineEnding kEndings[] = {LineEnding::CRLF, LineEnding::CR,  LineEnding::LF,
                                   LineEnding::LS,   LineEnding::PS, LineEnding::NEL};
constexpr int kEndingCount = static_cast<int>(std::size(kEndings));

std::string endingLabel(LineEnding e) {
  switch (e) {
    case LineEnding::LS: return "Unicode Line Separator (LS)";
    case LineEnding::PS: return "Unicode Paragraph Separator (PS)";
    case LineEnding::NEL: return "Next Line (NEL)";
    default: return LineEndingPrompt::endingName(e);
  }
}

std::string currentEndingText(const Document& d) {
  return d.buffer().mixedLineEndings() ? "mixed" : endingLabel(d.buffer().lineEnding());
}

// The encodings Reopen offers: the signature is a property of the bytes, so UTF-8 stands for both UTF-8 forms.
constexpr EncodingChoice kReopenChoices[] = {EncodingChoice::Utf8, EncodingChoice::Utf16LE, EncodingChoice::Utf16BE,
                                             EncodingChoice::Windows1252};
}  // namespace

EncodingPrompt::EncodingPrompt(PickerEnv& env, UnsavedPrompt& unsaved) : env_(env), unsaved_(unsaved) {}

EncodingPrompt::~EncodingPrompt() {
  life_.reset();
  close();
}

void EncodingPrompt::close() {
  if (popup_) popup_->close();
}

void EncodingPrompt::askReopen(Document& doc) {
  close();
  if (doc.isUntitled()) {
    env_.say(doc.path().filename().string() + " has no file on disk to reopen");
    return;
  }
  const fs::path file = doc.path();
  const EncodingChoice cur = doc.encodingChoice();
  auto p = std::make_unique<ListPopup>(false);
  p->setTitle("Reopen " + file.filename().string() + " with Encoding");
  std::vector<ListRow> rows;
  int selected = 0;
  for (EncodingChoice c : kReopenChoices) {
    ListRow r;
    r.key = std::string("enc:") + encodingChoiceSetting(c);
    r.primary = c == EncodingChoice::Utf8 ? "UTF-8" : encodingLabel(c);
    if (encodingOf(c) == encodingOf(cur)) {
      selected = static_cast<int>(rows.size());
      r.secondary = "current";
    }
    if (c == EncodingChoice::Windows1252) {
      bool exact = true;
      fallbackEncoding(0, &exact);
      if (!exact) r.detail = "stands for the system code page " + std::to_string(systemAnsiCodePage());
    }
    rows.push_back(std::move(r));
  }
  p->setMaxVisibleRows(static_cast<int>(rows.size()));
  p->setRows(std::move(rows), selected);
  p->onAccept([this, file, life = std::weak_ptr<int>(life_)](int row) {
    if (life.expired() || row < 0 || row >= static_cast<int>(std::size(kReopenChoices))) return;
    const EncodingChoice c = kReopenChoices[row];
    auto reopen = [this, file, c, life](bool) {
      if (life.expired()) return;
      Document* d = env_.editor.document(file);
      if (!d) return;
      std::string err;
      if (env_.editor.reopenWithEncoding(*d, c, &err))
        env_.say("Reopened " + file.filename().string() + " as " + encodingLabel(d->encodingChoice()));
      else
        env_.say("Cannot reopen " + file.filename().string() + " as " + encodingLabel(c) +
                 (err.empty() ? std::string() : ": " + err));
    };
    Document* d = env_.editor.document(file);
    if (d && d->dirty()) unsaved_.ask({file}, reopen);  // Save / Don't Save / Cancel
    else reopen(false);
  });
  p->onClosed([this, life = std::weak_ptr<int>(life_)] {
    if (!life.expired()) popup_ = nullptr;
  });
  popup_ = ListPopup::open(env_.root, std::move(p));
}

void EncodingPrompt::askSave(Document& doc, std::optional<EncodingChoice> preselect) {
  close();
  file_ = doc.path();
  enc_ = preselect.value_or(doc.encodingChoice());
  ending_ = 0;
  openSave(0);
}

void EncodingPrompt::openSave(int selected) {
  Document* d = env_.editor.document(file_);
  if (!d) return;
  auto p = std::make_unique<ListPopup>(false);
  p->setTitle("Advanced Save Options: " + file_.filename().string());
  std::vector<ListRow> rows;
  auto info = [&](const char* key, std::string text) {
    ListRow r;
    r.key = key;
    r.primary = std::move(text);
    r.dim = true;
    rows.push_back(std::move(r));
  };
  ListRow save;
  save.key = "save";
  save.primary = d->isUntitled() ? "Use this encoding" : "Save";
  save.secondary = std::string(encodingLabel(enc_)) + ", " +
                   (ending_ == 0 ? "Current Setting" : endingLabel(kEndings[ending_ - 1]));
  rows.push_back(std::move(save));
  info("head:enc", "Encoding:");
  for (size_t i = 0; i < std::size(kEncodingChoices); ++i) {
    const EncodingChoice c = kEncodingChoices[i];
    ListRow r;
    r.key = "enc:" + std::to_string(i);
    r.primary = std::string(c == enc_ ? "  (*) " : "  ( ) ") + encodingLabel(c);
    if (c == d->encodingChoice()) r.secondary = "current";
    rows.push_back(std::move(r));
  }
  info("head:le", "Line endings:");
  for (int i = 0; i <= kEndingCount; ++i) {
    ListRow r;
    r.key = "le:" + std::to_string(i);
    r.primary = std::string(i == ending_ ? "  (*) " : "  ( ) ") +
                (i == 0 ? "Current Setting" : endingLabel(kEndings[i - 1]));
    if (i == 0) r.secondary = currentEndingText(*d);
    rows.push_back(std::move(r));
  }
  p->setMaxVisibleRows(static_cast<int>(rows.size()));
  p->setRows(std::move(rows), selected);
  p->onAccept([this, life = std::weak_ptr<int>(life_)](int row) {
    if (life.expired()) return;
    if (!env_.editor.document(file_)) return;
    const int encFirst = 2, leFirst = encFirst + static_cast<int>(std::size(kEncodingChoices)) + 1;
    if (row == 0) {
      doSave();
    } else if (row >= encFirst && row < encFirst + static_cast<int>(std::size(kEncodingChoices))) {
      enc_ = kEncodingChoices[row - encFirst];
      openSave(row);
    } else if (row >= leFirst && row <= leFirst + kEndingCount) {
      ending_ = row - leFirst;
      openSave(row);
    } else {
      openSave(row);  // a heading
    }
  });
  p->onClosed([this, life = std::weak_ptr<int>(life_)] {
    if (!life.expired()) popup_ = nullptr;
  });
  popup_ = ListPopup::open(env_.root, std::move(p));
}

void EncodingPrompt::doSave() {
  Document* d = env_.editor.document(file_);
  if (!d) return;
  std::string err;
  UnencodableReport rep;
  std::optional<LineEnding> le;
  if (ending_ > 0) le = kEndings[ending_ - 1];
  const std::string name = file_.filename().string();
  if (env_.editor.saveWithEncoding(*d, enc_, le, &err, &rep)) {
    env_.say(d->isUntitled() ? name + " will be saved as " + encodingLabel(enc_)
                             : "Saved " + name + " as " + encodingLabel(d->encodingChoice()));
  } else if (!rep.empty()) {
    env_.refused({{file_, std::move(rep)}});  // the characters it cannot hold; the document is unchanged
  } else {
    env_.say("Cannot save " + name + (err.empty() ? std::string() : ": " + err));
  }
}

}  // namespace tf::ui
