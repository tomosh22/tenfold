#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/Editor.h"
#include "ui/widgets/TreeView.h"
#include "ui/widgets/Widget.h"

namespace tf::ui {

// Call hierarchy and type hierarchy (W11-GUI-LSP), one panel in the Workbench's bottom dock under the name
// "Hierarchy".
//
// Call hierarchy is Visual Studio's "View Call Hierarchy" (Ctrl+K, Ctrl+T): the root is the symbol at the caret, with
// the two nodes "Calls To 'x'" (its callers) and "Calls From 'x'" (what it calls); every caller or callee below them
// has the same two nodes (VS: "You can then expand each calling member to see its Calls To, and for C++, Calls From").
// Type hierarchy has no Visual Studio C++ window; tenfold adds it in the same panel: the root is the class at the caret
// with "Base Types" and "Derived Types". Every entry reads "name   file:line" (workspace-relative path; a call edge
// adds "(N calls)"). Type hierarchy entries have "Base Types" and "Derived Types" nodes in the same way. (Visual
// Studio's extra "Overrides" / "Implements" nodes for virtual members are not there.)
//
// Everything is lazy and asynchronous: opening the panel asks clangd for the symbol (Editor::callHierarchy /
// typeHierarchy); a node's children are requested when the node is first expanded (incomingCalls / outgoingCalls /
// supertypes / subtypes) and show "Loading..." until the answer arrives through Editor::post. Opening another
// hierarchy bumps a generation so the answers of the previous one are dropped; no request ever blocks the UI.
// The first group (callers / base types) is expanded as soon as the root is known.
//
// Keys of the focused tree: Up / Down / Left / Right / Home / End as in the explorer; Enter or a double click on an
// entry jumps to it (Editor::gotoHierarchyItem, so it lands in navigation history and Back returns); Escape closes
// the panel. The arrow (or Right / Left) expands without jumping.
//
// The panel talks to the engine through Backend, whose defaults call the Editor; tests replace the functions.
class HierarchyPanel : public Widget {
 public:
  enum class Mode { Call, Type };
  template <class T>
  using Done = Editor::Done<std::vector<T>>;
  struct Backend {
    std::function<void(Done<HierarchyItem>)> prepareCall, prepareType;
    std::function<void(const HierarchyItem&, Done<CallEdge>)> incoming, outgoing;
    std::function<void(const HierarchyItem&, Done<HierarchyItem>)> supertypes, subtypes;
    std::function<bool(const HierarchyItem&, std::string*)> goTo;
  };
  static Backend editorBackend(Editor& editor);

  explicit HierarchyPanel(Editor& editor);
  ~HierarchyPanel() override;

  void setBackend(Backend b) { backend_ = std::move(b); }
  // Returns why no hierarchy can be asked for now (empty: it can); open() then shows that text instead of asking.
  void setUnavailableFn(std::function<std::string()> fn) { unavailable_ = std::move(fn); }
  // Starts a new hierarchy at the caret (replacing the shown one). The panel does not show itself.
  void open(Mode mode);
  // Why nothing can be shown (clangd absent, a file outside every translation unit): the panel shows `text` as its
  // only entry instead of asking.
  void showMessage(Mode mode, const std::string& text);

  Mode mode() const { return mode_; }
  std::string title() const;   // "Call Hierarchy: foo" / "Type Hierarchy"
  std::string status() const;  // "Loading..." while a request is out, else empty
  TreeView& tree() { return *tree_; }
  bool busy() const { return pending_ > 0; }
  // Entry labels of the visible tree rows, in order (tests).
  std::vector<std::string> rowLabels() const;
  // The item of the entry of tree node `node` (null for headings and notes).
  const HierarchyItem* itemOf(int node) const;
  // Jumps to the entry of `node`; false for a node without a place or a failed jump.
  bool activate(int node);

  std::function<void()> onJumped;                     // after a successful jump
  std::function<void()> onClose;                      // Escape / the close button
  std::function<void(const std::string&)> onMessage;  // short notices (a failed jump)

  bool onEvent(const InputEvent& e) override;
  bool acceptsFocus() const override { return false; }  // the tree takes the focus
  void focusTree() { tree_->setFocus(); }

  RectF headerRect() const;
  RectF closeButtonRect() const;

 protected:
  void paint(PaintContext& ctx) override;
  void onLayout() override;

 private:
  enum class Dir { None, Incoming, Outgoing, Super, Sub };
  struct Entry {
    enum class Kind { Symbol, Group, Edge, Note } kind = Kind::Note;
    enum class State { NotLoaded, Loading, Loaded } state = State::NotLoaded;
    Dir dir = Dir::None;
    HierarchyItem item;  // Symbol / Edge
    size_t calls = 0;    // Edge: call sites
    std::string label;
    std::vector<std::unique_ptr<Entry>> kids;
  };
  float headerHeight() const;
  std::string itemLabel(const HierarchyItem& it) const;
  std::unique_ptr<Entry> symbolEntry(const HierarchyItem& it, bool edge) const;
  static std::unique_ptr<Entry> note(const std::string& text);
  void setRoot(std::unique_ptr<Entry> root);
  void rebuild();
  int addNode(TreeModel& m, int parent, Entry& e);
  void loadExpanded();
  void load(Entry& e);
  void finish(Entry& e, std::vector<std::unique_ptr<Entry>> kids, const std::string& error);

  Editor& editor_;
  Backend backend_;
  std::function<std::string()> unavailable_;
  TreeView* tree_ = nullptr;
  Mode mode_ = Mode::Call;
  std::unique_ptr<Entry> root_;
  std::vector<Entry*> nodes_;  // tree node -> entry
  std::string rootName_;
  int gen_ = 0, pending_ = 0;
  bool closeHot_ = false;
  std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace tf::ui
