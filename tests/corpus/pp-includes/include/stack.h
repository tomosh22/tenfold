#pragma once
// .inl files included inside a class body and at the end of the header; an extensionless header.
class /*@Stack*/Stack {
 public:
#include "stack_members.inl"
  int /*@Stack_top*/top() const;
  void /*@Stack_push*/push(int v);
 private:
  int /*@Stack_items*/items[8];
};
#include "stack_impl.inl"
