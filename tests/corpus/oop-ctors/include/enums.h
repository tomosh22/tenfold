#pragma once
enum /*@Color*/Color { /*@Color_Red*/Red, /*@Color_Green*/Green, /*@Color_Blue*/Blue };
enum class /*@Mode*/Mode { /*@Mode_Red*/Red, /*@Mode_Fast*/Fast, /*@Mode_Blue*/Blue };
enum /*@Level*/Level : unsigned char { /*@Level_Low*/Low = 1, /*@Level_High*/High = 2 };
namespace ui {
enum class /*@ui_Color*/Color { /*@ui_Color_Red*/Red, /*@ui_Color_Green*/Green };
} // namespace ui
