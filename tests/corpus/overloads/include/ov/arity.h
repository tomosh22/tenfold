#pragma once
#include <string>

namespace ov {

// Overloads by arity: declared here, defined in src/arity.cpp.
int /*@ar0_decl*/area();
int /*@ar1_decl*/area(int side);
int /*@ar2_decl*/area(int width, int height);

// Default arguments: the one-argument call picks the const char* version (array-to-pointer is an exact match, the
// std::string version needs a user-defined conversion).
void /*@log_c_decl*/log(const char* msg, int level = 1);
void /*@log_s_decl*/log(const std::string& msg);

// Function templates compete with plain functions: the non-template wins ties, T* is more specialised than T.
template <class T> int /*@pick_t*/pick(T) { return 1; }
template <class T> int /*@pick_p*/pick(T*) { return 2; }
inline int /*@pick_i*/pick(int) { return 3; }

}  // namespace ov
