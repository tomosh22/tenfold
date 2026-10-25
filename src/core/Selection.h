#pragma once
#include "core/Position.h"

namespace tf {

// A selection in a text buffer: `anchor` is where it started, `head` is the caret (the end that moves when the
// selection is extended). An empty selection (anchor == head) is a plain caret. The head may precede the anchor
// (a "reversed" selection made by extending leftwards / upwards).
struct Selection {
  Position anchor;
  Position head;

  bool empty() const { return anchor == head; }
  bool reversed() const { return head < anchor; }
  Position min() const { return head < anchor ? head : anchor; }
  Position max() const { return head < anchor ? anchor : head; }
  // The covered range with start <= end, regardless of direction.
  Range range() const { return {min(), max()}; }
  bool operator==(const Selection&) const = default;
};

inline Selection caretAt(Position p) { return {p, p}; }

}  // namespace tf
