#pragma once
// Late-bound COM helpers (IDispatch only; no #import, no generated headers) for the Visual Studio debugger backend.
// This header pulls in the Windows COM headers: include it only from *_win32.cpp files (and Windows-only tests).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <ole2.h>
#include <oleauto.h>
#include <windows.h>

#include <string>
#include <utility>
#include <vector>

#include "debug/VsDteUtil.h"

namespace tf::com {

std::wstring widen(const std::string& utf8);
std::string narrow(const wchar_t* w, size_t len);
inline std::string narrow(const std::wstring& w) { return narrow(w.data(), w.size()); }

// Owning BSTR.
class Bstr {
 public:
  Bstr() = default;
  explicit Bstr(const std::string& utf8);
  explicit Bstr(const std::wstring& w) : b_(SysAllocStringLen(w.data(), static_cast<UINT>(w.size()))) {}
  Bstr(const Bstr& o) : b_(o.b_ ? SysAllocStringLen(o.b_, SysStringLen(o.b_)) : nullptr) {}
  Bstr(Bstr&& o) noexcept : b_(o.b_) { o.b_ = nullptr; }
  Bstr& operator=(Bstr o) noexcept {
    std::swap(b_, o.b_);
    return *this;
  }
  ~Bstr() { SysFreeString(b_); }
  BSTR get() const { return b_; }
  BSTR* put() {  // for out parameters; frees the previous value
    SysFreeString(b_);
    b_ = nullptr;
    return &b_;
  }
  BSTR release() {
    BSTR t = b_;
    b_ = nullptr;
    return t;
  }
  UINT length() const { return SysStringLen(b_); }
  std::string toUtf8() const { return b_ ? narrow(b_, SysStringLen(b_)) : std::string(); }

 private:
  BSTR b_ = nullptr;
};

class Dispatch;

// Owning VARIANT.
class Variant {
 public:
  Variant() { VariantInit(&v_); }
  Variant(const Variant& o) {
    VariantInit(&v_);
    VariantCopy(&v_, const_cast<VARIANT*>(&o.v_));
  }
  Variant(Variant&& o) noexcept {
    v_ = o.v_;
    VariantInit(&o.v_);
  }
  Variant& operator=(Variant o) noexcept {
    std::swap(v_, o.v_);
    return *this;
  }
  ~Variant() { VariantClear(&v_); }

  static Variant fromString(const std::string& utf8);
  static Variant fromLong(long v);
  static Variant fromBool(bool v);
  static Variant fromDispatch(IDispatch* d);
  static Variant missing();  // an omitted optional argument (VT_ERROR / DISP_E_PARAMNOTFOUND)

  VARTYPE type() const { return V_VT(&v_); }
  bool isEmpty() const { return type() == VT_EMPTY || type() == VT_NULL; }
  VARIANT* put() {  // for out parameters; clears the previous value
    VariantClear(&v_);
    return &v_;
  }
  const VARIANT& raw() const { return v_; }

  // Conversions via VariantChangeType; false when the value does not convert.
  bool toString(std::string& out) const;
  bool toLong(long& out) const;
  bool toBool(bool& out) const;
  Dispatch toDispatch() const;  // empty Dispatch when the value is not an object

 private:
  VARIANT v_;
};

// A late-bound COM object. All calls report an HRESULT; the optional `detail` receives the server's exception text.
class Dispatch {
 public:
  Dispatch() = default;
  explicit Dispatch(IDispatch* p, bool addRef = true) : p_(p) {
    if (p_ && addRef) p_->AddRef();
  }
  Dispatch(const Dispatch& o) : p_(o.p_) {
    if (p_) p_->AddRef();
  }
  Dispatch(Dispatch&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
  Dispatch& operator=(Dispatch o) noexcept {
    std::swap(p_, o.p_);
    return *this;
  }
  ~Dispatch() {
    if (p_) p_->Release();
  }
  explicit operator bool() const { return p_ != nullptr; }
  IDispatch* get() const { return p_; }

  HRESULT getProperty(const wchar_t* name, Variant& out, std::string* detail = nullptr) const;
  HRESULT getProperty(const wchar_t* name, const std::vector<Variant>& args, Variant& out,
                      std::string* detail = nullptr) const;  // indexed property
  HRESULT putProperty(const wchar_t* name, const Variant& value, std::string* detail = nullptr) const;
  // Method call (also accepts a parameterized property). `out` may be null.
  HRESULT call(const wchar_t* name, const std::vector<Variant>& args, Variant* out,
               std::string* detail = nullptr) const;
  // Convenience: property that yields an object.
  HRESULT getObject(const wchar_t* name, Dispatch& out, std::string* detail = nullptr) const;
  HRESULT getString(const wchar_t* name, std::string& out, std::string* detail = nullptr) const;
  HRESULT getLong(const wchar_t* name, long& out, std::string* detail = nullptr) const;
  HRESULT getBool(const wchar_t* name, bool& out, std::string* detail = nullptr) const;

 private:
  HRESULT invoke(const wchar_t* name, WORD flags, const std::vector<Variant>& args, Variant* out,
                 std::string* detail) const;
  IDispatch* p_ = nullptr;
};

// A Visual Studio instance found in the Running Object Table.
struct DteInstance {
  DteMoniker info;
  std::string displayName;
  Dispatch dte;
};

// Every `!VisualStudio.DTE.<ver>:<pid>` object in the ROT that can be bound, newest first. COM must be initialised on
// the calling thread.
std::vector<DteInstance> enumerateDteInstances();

// Display names of the ROT entries that mention "VisualStudio" or "devenv" (any case), bound or not: diagnostics.
std::vector<std::string> enumerateRotNames();

}  // namespace tf::com
