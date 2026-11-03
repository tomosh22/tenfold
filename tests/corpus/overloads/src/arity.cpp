#include "ov/arity.h"

namespace ov {

int /*@ar0_def*/area() { return 0; }
int /*@ar1_def*/area(int side) { return side * side; }
int /*@ar2_def*/area(int width, int height) { return width * height; }

void /*@log_c_def*/log(const char* msg, int level) { (void)msg; (void)level; }
void /*@log_s_def*/log(const std::string& msg) { /*@use_log_c_in_s*/log(msg.c_str(), 2); }

}  // namespace ov
