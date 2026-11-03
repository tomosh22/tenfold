#pragma once
namespace io {
struct /*@Out*/Out {
  int n = 0;
  Out& /*@Out_shl_int*/operator<<(int v) { n += v; return *this; }
};
} // namespace io
