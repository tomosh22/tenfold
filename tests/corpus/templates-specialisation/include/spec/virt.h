#pragma once

namespace spec {

// Virtual functions of a class template: overriders live in classes derived from different instantiations.
template <class T>
struct /*@Task*/Task {
  virtual ~Task() = default;
  virtual T /*@Task_run*/run() = 0;
  virtual T /*@Task_name*/name() { return T(); }
};

struct /*@IntTask*/IntTask : Task<int> {
  int /*@IntTask_run*/run() override { return 1; }
};
struct /*@BigTask*/BigTask : IntTask {
  int /*@BigTask_run*/run() override { return 2; }
  int /*@BigTask_name*/name() override { return 3; }
};
struct /*@FloatTask*/FloatTask : Task<float> {
  float /*@FloatTask_run*/run() override { return 1.5f; }
};

}  // namespace spec
