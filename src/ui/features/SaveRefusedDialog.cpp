#include "ui/features/SaveRefusedDialog.h"

#include <algorithm>

namespace tf::ui {

namespace {

std::string countOf(size_t n, const char* one, const char* many) {
  return std::to_string(n) + " " + (n == 1 ? one : many);
}

// The character as text to draw: itself when it is a printable character, a stand-in otherwise.
std::string glyphOf(const UnencodableChar& c) {
  if (c.text.empty() || c.codePoint < 0x20 || (c.codePoint >= 0x7F && c.codePoint < 0xA0)) return "\xEF\xBF\xBD";
  return c.text;
}

}  // namespace

std::string SaveRefusedDialog::statusText(const std::vector<SaveRefusal>& refusals, const std::filesystem::path& root) {
  if (refusals.empty()) return {};
  if (refusals.size() == 1)
    return relativePath(root, refusals[0].file) + " was not saved: " + describeUnencodable(refusals[0].report);
  std::string names;
  for (size_t i = 0; i < refusals.size() && i < 3; ++i) names += (i ? ", " : "") + refusals[i].file.filename().string();
  if (refusals.size() > 3) names += ", ...";
  return std::to_string(refusals.size()) + " files were not saved: their encodings cannot hold some characters (" +
         names + ")";
}

void SaveRefusedDialog::show(std::vector<SaveRefusal> refusals) {
  close();
  refusals.erase(
      std::remove_if(refusals.begin(), refusals.end(), [](const SaveRefusal& r) { return r.report.empty(); }),
      refusals.end());
  if (refusals.empty()) return;
  const std::filesystem::path root = env_.editor.workspace().root();
  const bool many = refusals.size() > 1;
  auto p = std::make_unique<ListPopup>(false);
  p->setTitle(many ? countOf(refusals.size(), "file", "files") + " NOT saved"
                   : refusals[0].file.filename().string() + " was NOT saved");
  p->setStatus(many ? "nothing was written" : encodingName(refusals[0].report.encoding));
  std::vector<ListRow> rows;
  auto info = [&](std::string text) {
    ListRow r;
    r.key = "info";
    r.primary = std::move(text);
    r.dim = true;
    rows.push_back(std::move(r));
  };
  if (many) {
    info("These files hold characters their encoding cannot represent. Nothing was written to them.");
  } else {
    const UnencodableReport& rep = refusals[0].report;
    info(std::string(encodingName(rep.encoding)) + " cannot hold " + countOf(rep.total, "character", "characters") +
         " of this file. Nothing was written; the file on disk is unchanged.");
  }
  info("Enter goes to the selected character. Remove or replace it, then save again.");
  int firstChar = -1;
  std::vector<Location> where(rows.size());  // per row: where it goes (file empty for the explanation rows)
  const size_t perFile = many ? kCharsPerManyFiles : kCharsPerFile;
  for (size_t f = 0; f < refusals.size() && f < kMaxFiles; ++f) {
    const SaveRefusal& r = refusals[f];
    if (many)
      info(relativePath(root, r.file) + ": " + encodingName(r.report.encoding) + " cannot hold " +
           countOf(r.report.total, "character", "characters"));
    const size_t shown = std::min(perFile, r.report.chars.size());
    for (size_t i = 0; i < shown; ++i) {
      const UnencodableChar& c = r.report.chars[i];
      ListRow row;
      row.key = "c:" + std::to_string(f) + ":" + std::to_string(i);
      row.primary = (many ? "    " : "  ") + glyphOf(c) + "    " + unencodableLabel(c);
      row.detail = "line " + std::to_string(c.line + 1) + ", column " + std::to_string(c.column + 1);
      if (firstChar < 0) firstChar = static_cast<int>(rows.size());
      rows.push_back(std::move(row));
      where.resize(rows.size());
      where.back() = {r.file, {c.line, c.column}};
    }
    if (r.report.total > shown) info("    ... and " + std::to_string(r.report.total - shown) + " more");
    where.resize(rows.size());
  }
  if (refusals.size() > kMaxFiles) info("... and " + countOf(refusals.size() - kMaxFiles, "more file", "more files"));
  p->setMaxVisibleRows(static_cast<int>(std::min<size_t>(rows.size(), 16)));
  p->setRows(rows, std::max(firstChar, 0));
  where.resize(rows.size());
  const Location first = firstChar >= 0 ? where[static_cast<size_t>(firstChar)] : Location{};
  p->onAccept([this, where, first, life = std::weak_ptr<int>(life_)](int row) {
    if (life.expired() || row < 0 || static_cast<size_t>(row) >= where.size()) return;
    const Location& to = where[static_cast<size_t>(row)].file.empty() ? first : where[static_cast<size_t>(row)];
    std::string err;
    if (!to.file.empty() && !env_.editor.gotoLocation(to, &err, "unencodable character"))
      env_.say("Cannot open " + to.file.string() + (err.empty() ? std::string() : ": " + err));
  });
  p->onClosed([this, life = std::weak_ptr<int>(life_)] {
    if (!life.expired()) popup_ = nullptr;
  });
  popup_ = ListPopup::open(env_.root, std::move(p));
}

void SaveRefusedDialog::close() {
  if (popup_) popup_->close();
}

}  // namespace tf::ui
