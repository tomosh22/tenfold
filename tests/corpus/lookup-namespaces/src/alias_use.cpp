#include "alias_hdr.h"

int alias_uses() {
  al::/*@use_B3*/B3 b3;
  al::/*@use_B2*/B2 b2;
  al::/*@use_B1*/B1 b1;
  al::/*@use_Hold*/Hold<int> hold;
  al::/*@use_myint*/myint mi = 1;
  al_user::/*@use_us_Gadget*/Gadget ug;
  return b3.g + b2.g + b1.g + hold.v + mi + ug.g + al_user::/*@use_us_twice*/twice(2);
}
int al::/*@al_twice_def*/twice(int x) { return 2 * x; }
