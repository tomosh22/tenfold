#pragma once
namespace shape {
struct /*@Circle*/Circle { int r = 1; };
struct /*@Square*/Square { int s = 1; };
void /*@shape_draw_circle*/draw(const Circle&);
void /*@shape_draw_square*/draw(const Square&);
} // namespace shape
namespace other {
void /*@other_draw*/draw(const shape::Circle&);
}
// Global overload that ADL on shape types must not prefer.
void /*@global_draw*/draw(int);
