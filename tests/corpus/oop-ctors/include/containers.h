#pragma once
struct /*@Node*/Node { int /*@Node_get*/get() const { return 0; } };     // global Node
class /*@List*/List {
 public:
  class /*@List_Node*/Node {
   public:
    int /*@List_Node_get*/get() const { return v; }
    /*@use_Node_in_List*/Node* next = nullptr;
    int v = 0;
  };
  struct /*@List_Iter*/Iter {
    /*@use_Node_in_Iter*/Node* /*@List_Iter_n*/n;
    /*@use_Node_in_deref*/Node& /*@List_Iter_deref*/operator*() const { return *n; }
    Iter& /*@List_Iter_inc*/operator++() { n = n->next; return *this; }
    bool /*@List_Iter_ne*/operator!=(const Iter& o) const { return n != o.n; }
  };
  Iter /*@List_begin*/begin() { return Iter{head}; }
  Iter /*@List_end*/end() { return Iter{nullptr}; }
  int /*@List_size*/size() const { return n_; }
  /*@use_Node_head*/Node* head = nullptr;
  int n_ = 0;
};
class /*@Tree*/Tree {
 public:
  struct /*@Tree_Node*/Node { int /*@Tree_Node_get*/get() const { return key; } int key = 0; /*@use_Node_l*/Node* l = nullptr; };
  /*@use_Node_in_Tree*/Node* /*@Tree_root*/root = nullptr;
  int /*@Tree_size*/size() const { return 0; }
};
