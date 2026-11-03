#pragma once
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "app/Editor.h"
#include "ui/widgets/Panel.h"
#include "ui/widgets/Splitter.h"
#include "ui/widgets/StatusBar.h"

namespace tf::ui {

// The editor window layout: a tree of Splitters and Panels over a status bar, plus the mapping from tabs to open
// Editor documents. A tab is a *view* onto an Editor Document; the same document may be open in several panels (each
// with its own view widget from makeDocumentView). Tab ids are unique across the workbench and survive moves.
//
// Editor sync (constraint: a tab switch is not a navigation jump):
//  * Switching tabs/panels calls Editor::activateDocument, which does not touch navigation history.
//  * Navigation through the Editor (gotoLocation, back, forward) changes Editor::currentDocument(); the Editor has no
//    change callback, so call sync() once per frame / event batch: it reveals the current document's tab (the active
//    panel's if it has one, else another panel's, else a new tab in the active panel) and refreshes dirty markers and
//    the status bar. Opening a file as a *jump* is the caller's business (editor.gotoLocation, then sync()).
//  * Closing the last tab of a document closes it in the Editor unless it is dirty. A user-initiated close (the tab's
//  x,
//    middle click, requestCloseTab) of the last view of a dirty document first calls `confirmClose` when the front end
//    set it (EditorApp: Save / Don't Save / Cancel); the programmatic closeTab never asks, and closeTab(id, true)
//    discards the changes (the document is closed in the Editor too).
// Split semantics: splitting duplicates the active tab into the new panel (like a "split view" in 10x/VS); the new
// panel becomes active. Dragging a tab onto a content edge moves it into a new split. A panel emptied by a drag is
// removed; closing the last tab by hand leaves an empty panel. Key bindings are B4's: everything here is a method.
class Workbench : public RootWidget {
 public:
  enum class Drop { None, Strip, Center, Left, Right, Top, Bottom };
  struct DropTarget {
    Panel* panel = nullptr;
    Drop kind = Drop::None;
    int index = -1;  // Strip: insertion index
    RectF rect{0, 0, 0, 0};
  };

  Workbench(gfx::TextRenderer& text, Editor& editor, const Theme& theme = Theme::darkDefault());
  ~Workbench() override;

  // ---- panels ----
  std::vector<Panel*> panels() const;  // left-to-right / top-to-bottom
  Panel* activePanel() const { return active_; }
  void setActivePanel(Panel* p);
  Panel* splitActive(Splitter::Orientation o) { return splitPanel(active_, o, true, -1); }
  // New panel after/before `target` along `o`: moves tab `moveId` into it, or (-1) duplicates target's active tab.
  // `duplicateAll` (with moveId < 0): the new panel gets a view of every tab of target, in order, pinned flags and
  // active tab included (DuplicatePanel); otherwise only target's active tab.
  Panel* splitPanel(Panel* target, Splitter::Orientation o, bool after, int moveId, bool duplicateAll = false);
  bool closePanel(Panel* p);  // its tabs move to the nearest neighbour; false for the last panel

  // ---- panel navigation and layout moves (W8b) ----
  enum class Side { Left, Right, Up, Down };
  // The panel next to `from` in a direction by geometry (the panels' laid-out rectangles): among the panels that lie
  // wholly beyond that edge and overlap `from` across it, the closest one, then the one with the largest overlap, then
  // the first in layout order. Null when there is none (also in focus mode, where only one panel has an area).
  Panel* panelToward(Panel* from, Side d);
  bool focusPanelToward(Side d);  // MovePanelFocus*: makes that neighbour the active panel
  // MovePanel*: the active panel and its neighbour in that direction exchange places in the layout (sizes follow the
  // places); the active panel stays active. False when there is no neighbour.
  bool movePanelToward(Side d);
  // DuplicatePanel*: a copy of the panel with views of all its tabs next to it (after = to the right). `focusNew`
  // makes the copy the active panel. Null at the panel limit.
  Panel* duplicatePanel(Panel* p, bool after, bool focusNew);
  // PrevPanelTab / NextPanelTab: the neighbouring tab of the panel's strip, wrapping round. False with fewer than two.
  bool stepTab(Panel* p, int direction);
  // MovePrevPanelTab / MoveNextPanelTab: the active tab swaps places with its neighbour (no wrapping, never out of the
  // pinned / unpinned group). False when it cannot move.
  bool moveActiveTab(Panel* p, int direction);
  // Focus mode (ToggleFocusMode): only the active panel is shown, over the whole window; the status bar, the top bar,
  // the bottom dock and the other panels are hidden. Layout, tabs and splitter ratios are untouched, so leaving the
  // mode restores the window exactly. Session state, not saved.
  void setFocusMode(bool on);
  bool focusMode() const { return focusMode_; }

  // ---- tabs ----
  int openFile(const std::filesystem::path& path, Panel* in = nullptr);  // tab id or -1; focuses an existing tab
  // A tab for a document the Editor already holds (an untitled one from Editor::newUntitled); focuses an existing tab.
  int openDocument(Document& doc, Panel* in = nullptr);
  // An untitled document got a file (Editor::saveAs): its tabs follow, with the file name as title.
  void documentRenamed(const std::filesystem::path& from, const std::filesystem::path& to);
  // Shows `title` instead of the file name on the tabs of `file` (untitled files named after their first word); an
  // empty title removes it.
  void setTitleOverride(const std::filesystem::path& file, const std::string& title);
  bool closeTab(int id, bool discard = false);  // discard: also close a dirty document in the Editor (changes lost)
  // A close the user asked for. When `confirmClose` is set and the tab is the last view of a dirty document, it is
  // called instead of closing and returns true if it took over (it calls closeTab itself once the user agrees).
  bool requestCloseTab(int id);
  std::function<bool(int id)> confirmClose;
  bool isLastViewOfDirty(int id) const;
  // Files known to be gone from disk (EditorApp polls): their tabs get a "(deleted)" title suffix and the status bar a
  // "deleted on disk" segment while the document is current. The buffer stays.
  void setFileMissing(const std::filesystem::path& file, bool missing);
  bool fileMissing(const std::filesystem::path& file) const { return missing_.count(file.generic_string()) != 0; }
  void closeOthers(int id);          // other unpinned tabs of the same panel
  void closeAllButPinned(Panel* p);  // pinned tabs stay
  void setPinned(int id, bool pinned);
  bool moveTab(int id, Panel* dst, int index = -1);  // -1 appends; merges with an existing tab of the same file
  Panel* panelOf(int id) const;
  std::filesystem::path tabPath(int id) const;
  // Ctrl+Tab: walks the panel's MRU order (repeated calls go deeper, backward wraps to the oldest). A cycle ends when
  // endMruCycle() is called (Ctrl released) or the active tab is changed by anything else. Returns the active tab id.
  int cycleMru(Panel* p, bool forward);
  void endMruCycle() { cycle_ = {}; }

  // Files in the order they were last shown or opened (most recent first, at most kMaxRecent); fed by openFile, tab
  // switches and navigation, persisted in layout.json. Used by the open-file picker.
  const std::vector<std::filesystem::path>& recentFiles() const { return recent_; }
  void noteRecent(const std::filesystem::path& file);

  // ---- drag between panels ----
  void beginTabDrag(int id, float x, float y);  // normally fired by TabStrip::onBeginDragOut; captures the mouse
  DropTarget dropTargetAt(float x, float y) const;
  bool dragging() const { return dragTab_ >= 0; }

  // ---- Editor sync ----
  void sync();
  StatusBar& status() { return *status_; }
  // A thin bar above the panel tree. `bar` must be a child of the workbench (addChild) and is laid out in the top
  // `height` pixels; pass nullptr to remove the reservation. Not owned here.
  void setTopBar(Widget* bar, float height);

  // ---- bottom dock (C1-UI) ----
  // A resizable (Splitter) dock under the panel tree hosting one of several named panels; hidden until shown. The
  // panel widgets are owned by the dock and draw their own headers. Visibility, the shown panel's name and the
  // dock's height (a fraction of the area above the status bar) are saved in layout.json under "bottom"; a
  // restored state for a panel that is added later takes effect when it is added.
  Widget* addBottomPanel(const std::string& name,
                         std::unique_ptr<Widget> w);  // not shown yet; replaces a same-named one
  Widget* bottomPanel(const std::string& name) const;
  bool showBottomPanel(const std::string& name);  // false for an unknown name
  void hideBottomPanel();
  bool toggleBottomPanel(const std::string& name);  // hides if that panel is shown, else shows it; true = now visible
  bool bottomVisible() const;
  const std::string& bottomPanelName() const { return dock_.name; }  // last shown (or restored) panel
  float bottomHeight() const;                                        // 0..1 of the dock area, kept while hidden
  void setBottomHeight(float fraction);
  RectF bottomRect() const;  // window coordinates, empty when hidden

  // ---- left side dock (W8f, the workspace explorer) ----
  // Same shape as the bottom dock, on the left of the whole window area (between the top bar and the status bar):
  // hidden until shown, named panels of which one is visible, resizable by its divider. `width` is a fraction of the
  // window width (kept while hidden). Saved in layout.json under "left"; a layout without it (or a focus-mode
  // window) has the dock hidden.
  Widget* addSidePanel(const std::string& name, std::unique_ptr<Widget> w);  // not shown yet; replaces a same-named one
  Widget* sidePanel(const std::string& name) const;
  bool showSidePanel(const std::string& name);  // false for an unknown name
  void hideSidePanel();
  bool toggleSidePanel(const std::string& name);  // true = now visible
  bool sideVisible() const;
  const std::string& sidePanelName() const { return sideState_.name; }
  float sideWidth() const;
  void setSideWidth(float fraction);
  RectF sideRect() const;  // window coordinates, empty when hidden
  // The caret the tab of `file` last had when another tab was shown (line 0, column 0 when unknown).
  Position lastCaretOf(const std::filesystem::path& file) const;

  // ---- persistence (<workspace>/.tenfold/layout.json) ----
  std::filesystem::path layoutPath() const;
  std::string layoutJson() const;
  bool saveLayout() const;
  // Replaces the whole layout. False (default single-panel layout installed) for a missing/corrupt file.
  bool restoreLayout();
  bool restoreLayoutFromJson(const std::string& text);

  bool dispatch(const InputEvent& e);  // as RootWidget::dispatch, plus "focus follows click" for the active panel
  bool onEvent(const InputEvent& e) override;  // drag-out gesture (mouse is captured)

 protected:
  void onLayout() override;

 private:
  class Overlay;
  class DockSplit;
  class Dock;
  struct TabInfo {
    std::filesystem::path path;
  };
  struct Cycle {
    Panel* panel = nullptr;
    std::vector<int> order;
    size_t pos = 0;
    int expect = -1;
  };

  std::unique_ptr<Panel> makePanel();
  void installTree(std::unique_ptr<Widget> top, bool keepTabs = false);
  bool moveTabImpl(int id, Panel* dst, int index);
  std::unique_ptr<Widget> replaceNode(Widget* old, std::unique_ptr<Widget> nw);
  int addTabTo(Panel* p, Document& doc, bool pinned);
  int findTab(Panel* p, const Document& doc) const;
  Document* docOf(int id) const;
  void releaseDocument(const std::filesystem::path& path, bool discard = false);
  void showDocument(Document& d);
  void showActiveTab();
  void focusActiveView();
  bool revealDocument(Document& d);
  void applyDrop(int id, const DropTarget& t);
  void cancelDrag();
  void collect(Widget* w, std::vector<Panel*>& out) const;
  void refreshStatus();
  void applyDock();
  void applySide();
  void applyFocusMode();
  void swapPanels(Panel* a, Panel* b);
  std::string tabTitle(const std::filesystem::path& file) const;

  Editor& editor_;
  DockSplit* split_ = nullptr;  // body_ above, dock_ below
  Widget* body_ = nullptr;      // holds top_
  Dock* dockWidget_ = nullptr;
  DockSplit* side_ = nullptr;  // sideWidget_ | split_
  Dock* sideWidget_ = nullptr;
  struct SideState {
    bool visible = false;
    std::string name;
    float width = 0.22f;
  } sideState_;
  struct DockState {
    bool visible = false;
    std::string name;
    float height = 0.3f;
  } dock_;
  Widget* top_ = nullptr;
  StatusBar* status_ = nullptr;
  Widget* topBar_ = nullptr;
  float topBarH_ = 0;
  Overlay* overlay_ = nullptr;
  Panel* active_ = nullptr;
  std::map<int, TabInfo> tabs_;
  std::map<std::string, Position> lastCaret_;
  std::map<std::string, std::string> titles_;  // generic path -> tab title override
  bool focusMode_ = false;
  std::set<std::string> missing_;  // generic paths of open documents whose file vanished
  std::vector<std::filesystem::path> recent_;
  Document* lastCur_ = nullptr;
  int nextId_ = 1;
  int dragTab_ = -1;
  DropTarget drop_;
  Cycle cycle_;
};

}  // namespace tf::ui
