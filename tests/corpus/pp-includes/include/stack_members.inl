// Included in the middle of a class body.
int /*@Stack_size*/size() const { return count_; }
bool /*@Stack_empty*/empty() const { return count_ == 0; }
int /*@Stack_count*/count_ = 0;
