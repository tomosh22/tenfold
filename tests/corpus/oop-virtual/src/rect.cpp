#include "geom/rect.h"
double geom::Rect::/*@Rect_area_def*/area() const { return w_ * h_; }
geom::Rect* geom::Rect::/*@Rect_clone_def*/clone() const { return new Rect(*this); }
