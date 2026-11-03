#pragma once
namespace io {
struct /*@Readable*/Readable {
  virtual ~Readable() = default;
  virtual int /*@Readable_size*/size() const = 0;
  virtual int /*@Readable_read*/read() = 0;
};
struct /*@Writable*/Writable {
  virtual ~Writable() = default;
  virtual int /*@Writable_size*/size() const = 0;
  virtual void /*@Writable_write*/write(int) = 0;
};
// One override implements both interfaces' size().
class /*@Buffer*/Buffer : public Readable, public Writable {
 public:
  int /*@Buffer_size*/size() const override { return n_; }
  int /*@Buffer_read*/read() override { return n_ ? --n_ : 0; }
  void /*@Buffer_write*/write(int) override { ++n_; }
 private:
  int n_ = 0;
};
class /*@FileStream*/FileStream : public Readable {
 public:
  int /*@FileStream_size*/size() const override { return 100; }
  int /*@FileStream_read*/read() override { return 7; }
};
class /*@NullStream*/NullStream : public Writable {
 public:
  int /*@NullStream_size*/size() const override { return 0; }
  void /*@NullStream_write*/write(int) override {}
};
} // namespace io
