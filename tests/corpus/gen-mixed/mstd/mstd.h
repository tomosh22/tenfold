#pragma once
// Minimal stand-in for the few standard-library facilities the generated code uses (no system headers needed).
namespace mstd {

typedef unsigned long size_t;

template <typename T>
class vector {
public:
    vector() : data_(0), size_(0), cap_(0) {}
    ~vector() { delete[] data_; }
    void push_back(const T& v) {
        if (size_ == cap_) grow();
        data_[size_++] = v;
    }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    void clear() { size_ = 0; }
    T& operator[](size_t i) { return data_[i]; }

private:
    vector(const vector&);
    vector& operator=(const vector&);
    void grow() {
        size_t n = cap_ ? cap_ * 2 : 4;
        T* d = new T[n];
        for (size_t i = 0; i < size_; ++i) d[i] = data_[i];
        delete[] data_;
        data_ = d;
        cap_ = n;
    }
    T* data_;
    size_t size_;
    size_t cap_;
};

template <typename A, typename B>
struct pair {
    A first;
    B second;
};

template <typename T>
T min(T a, T b) { return b < a ? b : a; }

template <typename T>
T max(T a, T b) { return a < b ? b : a; }

}  // namespace mstd
