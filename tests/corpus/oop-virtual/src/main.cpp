#include "geom/circle.h"
#include "geom/rect.h"
#include "geom/triangle.h"
#include "io/streams.h"
#include "zoo/animals.h"
#include "pimpl/engine.h"
#include "pimpl/cache.h"

double total() {
  geom::Circle c(1.0);
  geom::Rect r(2, 3);
  geom::Square sq(2);
  geom::Triangle t;
  geom::Shape* shapes[] = {&c, &r, &sq, &t};
  double sum = 0;
  for (geom::Shape* s : shapes) {
    sum += s->/*@use_area_ptr*/area();
    sum += s->/*@use_sides_ptr*/sides();
    sum += s->/*@use_get_ptr*/get();
  }
  const geom::Shape& ref = c;
  const char* nm = ref./*@use_name_ref*/name();
  geom::Shape* /*@var_cl*/cl = r./*@use_clone_rect*/clone();   // covariant: Rect::clone
  geom::Shape* cl2 = sq./*@use_clone_sq*/clone();
  geom::Shape* cl3 = shapes[0]->/*@use_clone_ptr*/clone();
  double (geom::Shape::*pa)() const = &geom::Shape::/*@use_pm_area*/area;
  sum += (shapes[1]->*pa)();
  double geom::Circle::*pr = &geom::Circle::/*@use_pm_r*/r_;
  sum += c.*pr;
  sum += c./*@use_circle_get*/get() + sq./*@use_sq_area*/area() + t./*@use_tri_area*/area();
  sum += c./*@use_circle_size*/size();
  (void)nm; delete cl; delete cl2; delete cl3;
  return sum;
}

int streams() {
  io::Buffer buf;
  io::Readable* rd = &buf;
  io::Writable* wr = &buf;
  io::FileStream fs;
  io::NullStream ns;
  wr->/*@use_wr_write*/write(1);
  int a = rd->/*@use_rd_size*/size();
  int b = wr->/*@use_wr_size*/size();
  int c = buf./*@use_buf_size*/size();
  int d = fs./*@use_fs_size*/size() + ns./*@use_ns_size*/size();
  return a + b + c + d + rd->/*@use_rd_read*/read();
}

int zoo_test() {
  zoo::Duck d;
  zoo::Animal& /*@var_a*/a = d;
  zoo::Walker& w = d;
  d./*@use_duck_walk*/walk();
  d./*@use_duck_swim*/swim();
  int legs = d./*@use_duck_legs*/legs;
  const char* s = a./*@use_animal_speak*/speak();
  const char* s2 = w./*@use_walker_speak*/speak();
  const char* s3 = d./*@use_duck_speak*/speak();
  (void)s; (void)s2; (void)s3;
  return legs + d./*@use_duck_get*/get();
}

int pimpl() {
  /*@use_Engine*/Engine e;
  Cache c;
  return e./*@use_Engine_size*/size() + c./*@use_Cache_size*/size() + e./*@use_Engine_get*/get();
}
