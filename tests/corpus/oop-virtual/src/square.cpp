#include "geom/rect.h"
double geom::Square::/*@Square_area_def*/area() const { return w_ * w_; }
geom::Square* geom::Square::/*@Square_clone_def*/clone() const { return new Square(*this); }
