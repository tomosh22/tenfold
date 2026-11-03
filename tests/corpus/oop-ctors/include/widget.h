#pragma once
class /*@Widget*/Widget {
 public:
  /*@Widget_ctor0*/Widget();
  explicit /*@Widget_ctor_int*/Widget(int size);
  /*@Widget_ctor_wh*/Widget(int w, int h);
  /*@Widget_copy*/Widget(const Widget&);
  /*@Widget_move*/Widget(Widget&&) noexcept;
  Widget& /*@Widget_copy_assign*/operator=(const Widget&);
  Widget& /*@Widget_move_assign*/operator=(Widget&&) noexcept;
  /*@Widget_dtor*/~Widget();
  /*@Widget_to_bool*/operator bool() const;
  explicit /*@Widget_to_int*/operator int() const;
  int /*@Widget_size*/size() const { return size_; }
  int /*@Widget_get*/get() const;
  static int /*@Widget_live*/live;
  static Widget /*@Widget_make*/make(int);
 private:
  int size_;
};
