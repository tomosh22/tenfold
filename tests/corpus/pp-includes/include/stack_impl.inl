inline int Stack::/*@Stack_top_def*/top() const { return items[count_ - 1]; }
inline void Stack::/*@Stack_push_def*/push(int v) { items[count_++] = v; }
