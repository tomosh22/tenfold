#pragma once
class /*@Registry*/Registry {
 public:
  static int /*@Registry_count*/count;
  static const int /*@Registry_max*/max = 8;
  static Registry& /*@Registry_instance*/instance();
  static void /*@Registry_add*/add(int);
  int /*@Registry_get*/get() const { return last_; }
 private:
  int last_ = 0;
};
