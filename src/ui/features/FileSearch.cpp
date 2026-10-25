#include "ui/features/FileSearch.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>

#include "semantic/FastIndex.h"

namespace tf::ui {

namespace {
constexpr int kNameWeight = 4;
constexpr int kRecentBonus = 200, kRecentStep = 15;  // bonus = 200 - 15 * rank for the 14 most recent files
}  // namespace

std::vector<std::string> FileSearch::collect(const Workspace& ws) {
  std::vector<std::string> out;
  out.reserve(ws.fileCount());
  const std::string root = ws.root().generic_string();
  for (const auto* list : {&ws.sourceFiles(), &ws.headerFiles(), &ws.otherFiles()})
    for (const auto& p : *list) out.push_back(relativePath(root, p));
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<size_t> FileSearch::rank(const std::vector<std::string>& files, const std::vector<std::string>& recent,
                                     const std::string& query, size_t limit) {
  std::vector<size_t> out;
  std::vector<std::string_view> tokens;  // whitespace separated; every token must match
  for (size_t i = 0; i < query.size();) {
    while (i < query.size() && query[i] == ' ') ++i;
    size_t j = i;
    while (j < query.size() && query[j] != ' ') ++j;
    if (j > i) tokens.emplace_back(query.data() + i, j - i);
    i = j;
  }
  std::unordered_map<std::string_view, int> recentRank;
  for (size_t i = 0; i < recent.size(); ++i) recentRank.emplace(recent[i], static_cast<int>(i));

  if (tokens.empty()) {  // no query: recent files in MRU order, then the rest in list order
    std::unordered_map<std::string_view, size_t> where;
    where.reserve(files.size());
    for (size_t i = 0; i < files.size(); ++i) where.emplace(files[i], i);
    std::vector<bool> used(files.size(), false);
    for (const std::string& r : recent) {
      auto it = where.find(r);
      if (it != where.end() && !used[it->second] && out.size() < limit) {
        used[it->second] = true;
        out.push_back(it->second);
      }
    }
    for (size_t i = 0; i < files.size() && out.size() < limit; ++i)
      if (!used[i]) out.push_back(i);
    return out;
  }

  struct Scored {
    int score;
    size_t index;
  };
  std::vector<Scored> hits;
  for (size_t i = 0; i < files.size(); ++i) {
    const std::string& rel = files[i];
    const std::string_view name =
        std::string_view(rel).substr(rel.rfind('/') == std::string::npos ? 0 : rel.rfind('/') + 1);
    int total = 0;
    for (std::string_view t : tokens) {
      const int sn = FastIndex::fuzzyScore(t, name), sp = FastIndex::fuzzyScore(t, rel);
      const int s = std::max(kNameWeight * sn, sp);
      if (s <= 0) {
        total = 0;
        break;
      }
      total += s;
    }
    if (total <= 0) continue;
    auto it = recentRank.find(rel);
    if (it != recentRank.end()) total += std::max(0, kRecentBonus - kRecentStep * it->second);
    hits.push_back({total, i});
  }
  auto better = [&](const Scored& a, const Scored& b) {
    if (a.score != b.score) return a.score > b.score;
    if (files[a.index].size() != files[b.index].size()) return files[a.index].size() < files[b.index].size();
    return a.index < b.index;
  };
  const size_t n = std::min(limit, hits.size());
  std::partial_sort(hits.begin(), hits.begin() + static_cast<std::ptrdiff_t>(n), hits.end(), better);
  for (size_t i = 0; i < n; ++i) out.push_back(hits[i].index);
  return out;
}

FileSearch::FileSearch(PickerEnv& env, Workbench& bench) : env_(env), bench_(bench) {}

void FileSearch::open() {
  close();
  files_ = collect(env_.editor.workspace());
  recent_.clear();
  for (const auto& p : bench_.recentFiles()) recent_.push_back(relativePath(env_.editor.workspace().root(), p));
  auto p = std::make_unique<ListPopup>(true);
  p->input()->setPlaceholder("Open file");
  p->onQuery([this](const std::string&) { refresh(); });
  p->onAccept([this](int row) {
    if (row < 0 || row >= static_cast<int>(shown_.size())) return;
    const std::string& rel = files_[shown_[static_cast<size_t>(row)]];
    if (bench_.openFile(env_.editor.workspace().root() / rel) < 0) env_.say("Cannot open " + rel);
  });
  p->onClosed([this] { popup_ = nullptr; });
  popup_ = ListPopup::open(env_.root, std::move(p));
  refresh();
}

void FileSearch::close() {
  if (popup_) popup_->close();
}

void FileSearch::refresh() {
  const std::string q = popup_->input()->text();
  shown_ = rank(files_, recent_, q);
  std::string compact;  // highlight positions for the filename: the query without blanks
  for (char c : q)
    if (c != ' ') compact += c;
  std::vector<ListRow> rows;
  for (size_t idx : shown_) {
    const std::string& rel = files_[idx];
    const size_t slash = rel.rfind('/');
    ListRow r;
    r.key = rel;
    r.primary = slash == std::string::npos ? rel : rel.substr(slash + 1);
    r.secondary = slash == std::string::npos ? std::string() : rel.substr(0, slash);
    r.match = fuzzyMatchPositions(compact, r.primary);
    if (std::find(recent_.begin(), recent_.end(), rel) != recent_.end()) r.badge = "recent";
    rows.push_back(std::move(r));
  }
  popup_->setStatus(std::to_string(files_.size()) + " files");
  popup_->setRows(std::move(rows), 0);
}

}  // namespace tf::ui
