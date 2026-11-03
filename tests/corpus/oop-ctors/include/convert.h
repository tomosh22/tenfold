#pragma once
struct /*@Meters*/Meters {
  explicit /*@Meters_ctor*/Meters(double v) : v(v) {}
  double /*@Meters_v*/v;
};
struct /*@Feet*/Feet {
  /*@Feet_from_Meters*/Feet(Meters m) : v(m.v * 3.28) {}     // implicit conversion Meters -> Feet
  /*@Feet_to_Meters*/operator Meters() const { return Meters(v / 3.28); }
  explicit /*@Feet_to_double*/operator double() const { return v; }
  double /*@Feet_v*/v;
};
double /*@take_feet*/take_feet(Feet f);
double /*@take_meters*/take_meters(Meters m);
