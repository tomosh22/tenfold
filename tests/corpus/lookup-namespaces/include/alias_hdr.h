#pragma once
// Aliases for "go to definition lets the user choose" (W10-CHOOSE): a typedef chain, an alias template, a typedef of a
// builtin type, a using-declaration of a class and of a function in a header.
namespace al {
struct /*@al_Gadget*/Gadget { int g = 0; };
typedef Gadget /*@al_B1*/B1;
typedef B1 /*@al_B2*/B2;
using /*@al_B3*/B3 = B2;
template <class T> struct /*@al_Holder*/Holder { T v; };
template <class T> using /*@al_Hold*/Hold = Holder<T>;
typedef int /*@al_myint*/myint;
int /*@al_twice*/twice(int x);
}  // namespace al
namespace al_user {
using al::/*@al_using_Gadget*/Gadget;
using al::/*@al_using_twice*/twice;
}  // namespace al_user
