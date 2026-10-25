#include "debug/ComDispatch_win32.h"

#include <algorithm>
#include <cctype>

namespace tf::com {

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring w(static_cast<size_t>(n), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string narrow(const wchar_t* w, size_t len) {
  if (!w || len == 0) return {};
  int n = ::WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string s(static_cast<size_t>(n), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), s.data(), n, nullptr, nullptr);
  return s;
}

Bstr::Bstr(const std::string& utf8) {
  std::wstring w = widen(utf8);
  b_ = SysAllocStringLen(w.data(), static_cast<UINT>(w.size()));
}

// ---- Variant ----

Variant Variant::fromString(const std::string& utf8) {
  Variant v;
  Bstr b(utf8);
  V_VT(&v.v_) = VT_BSTR;
  V_BSTR(&v.v_) = b.release();
  return v;
}
Variant Variant::fromLong(long x) {
  Variant v;
  V_VT(&v.v_) = VT_I4;
  V_I4(&v.v_) = static_cast<LONG>(x);
  return v;
}
Variant Variant::fromBool(bool x) {
  Variant v;
  V_VT(&v.v_) = VT_BOOL;
  V_BOOL(&v.v_) = x ? VARIANT_TRUE : VARIANT_FALSE;
  return v;
}
Variant Variant::fromDispatch(IDispatch* d) {
  Variant v;
  if (!d) return v;
  d->AddRef();
  V_VT(&v.v_) = VT_DISPATCH;
  V_DISPATCH(&v.v_) = d;
  return v;
}
Variant Variant::missing() {
  Variant v;
  V_VT(&v.v_) = VT_ERROR;
  V_ERROR(&v.v_) = DISP_E_PARAMNOTFOUND;
  return v;
}

bool Variant::toString(std::string& out) const {
  if (type() == VT_BSTR) {
    out = V_BSTR(&v_) ? narrow(V_BSTR(&v_), SysStringLen(V_BSTR(&v_))) : std::string();
    return true;
  }
  if (isEmpty()) return false;
  Variant t;
  if (FAILED(VariantChangeType(t.put(), const_cast<VARIANT*>(&v_), 0, VT_BSTR))) return false;
  out = V_BSTR(&t.v_) ? narrow(V_BSTR(&t.v_), SysStringLen(V_BSTR(&t.v_))) : std::string();
  return true;
}
bool Variant::toLong(long& out) const {
  if (isEmpty()) return false;
  Variant t;
  if (FAILED(VariantChangeType(t.put(), const_cast<VARIANT*>(&v_), 0, VT_I4))) return false;
  out = V_I4(&t.v_);
  return true;
}
bool Variant::toBool(bool& out) const {
  if (isEmpty()) return false;
  Variant t;
  if (FAILED(VariantChangeType(t.put(), const_cast<VARIANT*>(&v_), 0, VT_BOOL))) return false;
  out = V_BOOL(&t.v_) != VARIANT_FALSE;
  return true;
}
Dispatch Variant::toDispatch() const {
  if (type() == VT_DISPATCH) return Dispatch(V_DISPATCH(&v_));
  if (type() == VT_UNKNOWN && V_UNKNOWN(&v_)) {
    IDispatch* d = nullptr;
    if (SUCCEEDED(V_UNKNOWN(&v_)->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&d))))
      return Dispatch(d, false);
  }
  return Dispatch();
}

// ---- Dispatch ----

HRESULT Dispatch::invoke(const wchar_t* name, WORD flags, const std::vector<Variant>& args, Variant* out,
                         std::string* detail) const {
  if (!p_) return E_POINTER;
  DISPID id = 0;
  LPOLESTR n = const_cast<LPOLESTR>(name);
  HRESULT hr = p_->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id);
  if (FAILED(hr)) {
    if (detail) *detail = "unknown member " + narrow(name, wcslen(name));
    return hr;
  }
  // IDispatch wants the arguments right to left. Shallow copies: the Variants in `args` keep ownership.
  std::vector<VARIANTARG> rev(args.size());
  for (size_t i = 0; i < args.size(); ++i) rev[i] = args[args.size() - 1 - i].raw();
  DISPPARAMS dp{};
  dp.cArgs = static_cast<UINT>(rev.size());
  dp.rgvarg = rev.empty() ? nullptr : rev.data();
  DISPID putId = DISPID_PROPERTYPUT;
  if (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) {
    dp.cNamedArgs = 1;
    dp.rgdispidNamedArgs = &putId;
  }
  EXCEPINFO ex{};
  UINT argErr = 0;
  VARIANT* res = out ? out->put() : nullptr;
  hr = p_->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags, &dp, res, &ex, &argErr);
  if (hr == DISP_E_EXCEPTION) {
    if (detail) {
      std::string d = ex.bstrDescription ? narrow(ex.bstrDescription, SysStringLen(ex.bstrDescription)) : std::string();
      *detail = d;
    }
    HRESULT code = ex.scode ? ex.scode : (ex.wCode ? HRESULT_FROM_WIN32(ex.wCode) : E_FAIL);
    SysFreeString(ex.bstrSource);
    SysFreeString(ex.bstrDescription);
    SysFreeString(ex.bstrHelpFile);
    hr = code;
  }
  return hr;
}

HRESULT Dispatch::getProperty(const wchar_t* name, Variant& out, std::string* detail) const {
  return invoke(name, DISPATCH_PROPERTYGET, {}, &out, detail);
}
HRESULT Dispatch::getProperty(const wchar_t* name, const std::vector<Variant>& args, Variant& out,
                              std::string* detail) const {
  return invoke(name, DISPATCH_PROPERTYGET, args, &out, detail);
}
HRESULT Dispatch::putProperty(const wchar_t* name, const Variant& value, std::string* detail) const {
  return invoke(name, DISPATCH_PROPERTYPUT, {value}, nullptr, detail);
}
HRESULT Dispatch::call(const wchar_t* name, const std::vector<Variant>& args, Variant* out, std::string* detail) const {
  return invoke(name, DISPATCH_METHOD | DISPATCH_PROPERTYGET, args, out, detail);
}
HRESULT Dispatch::getObject(const wchar_t* name, Dispatch& out, std::string* detail) const {
  Variant v;
  HRESULT hr = getProperty(name, v, detail);
  if (FAILED(hr)) return hr;
  out = v.toDispatch();
  return out ? S_OK : E_NOINTERFACE;
}
HRESULT Dispatch::getString(const wchar_t* name, std::string& out, std::string* detail) const {
  Variant v;
  HRESULT hr = getProperty(name, v, detail);
  if (FAILED(hr)) return hr;
  if (!v.toString(out)) out.clear();  // VT_EMPTY / VT_NULL: an empty string
  return S_OK;
}
HRESULT Dispatch::getLong(const wchar_t* name, long& out, std::string* detail) const {
  Variant v;
  HRESULT hr = getProperty(name, v, detail);
  if (FAILED(hr)) return hr;
  return v.toLong(out) ? S_OK : DISP_E_TYPEMISMATCH;
}
HRESULT Dispatch::getBool(const wchar_t* name, bool& out, std::string* detail) const {
  Variant v;
  HRESULT hr = getProperty(name, v, detail);
  if (FAILED(hr)) return hr;
  return v.toBool(out) ? S_OK : DISP_E_TYPEMISMATCH;
}

// ---- Running Object Table ----

namespace {
template <class T>
class Ptr {
 public:
  Ptr() = default;
  ~Ptr() {
    if (p_) p_->Release();
  }
  Ptr(const Ptr&) = delete;
  Ptr& operator=(const Ptr&) = delete;
  T** put() {
    if (p_) p_->Release();
    p_ = nullptr;
    return &p_;
  }
  T* operator->() const { return p_; }
  T* get() const { return p_; }
  explicit operator bool() const { return p_ != nullptr; }

 private:
  T* p_ = nullptr;
};
}  // namespace

std::vector<std::string> enumerateRotNames() {
  std::vector<std::string> names;
  Ptr<IRunningObjectTable> rot;
  if (FAILED(GetRunningObjectTable(0, rot.put()))) return names;
  Ptr<IEnumMoniker> en;
  if (FAILED(rot->EnumRunning(en.put()))) return names;
  Ptr<IBindCtx> bc;
  if (FAILED(CreateBindCtx(0, bc.put()))) return names;
  for (;;) {
    Ptr<IMoniker> mk;
    ULONG got = 0;
    if (en->Next(1, mk.put(), &got) != S_OK || got == 0) break;
    LPOLESTR name = nullptr;
    if (FAILED(mk->GetDisplayName(bc.get(), nullptr, &name)) || !name) continue;
    std::string display = narrow(name, wcslen(name));
    CoTaskMemFree(name);
    std::string lower = display;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.find("visualstudio") != std::string::npos || lower.find("devenv") != std::string::npos)
      names.push_back(display);
  }
  return names;
}

std::vector<DteInstance> enumerateDteInstances() {
  std::vector<DteInstance> found;
  Ptr<IRunningObjectTable> rot;
  if (FAILED(GetRunningObjectTable(0, rot.put()))) return found;
  Ptr<IEnumMoniker> en;
  if (FAILED(rot->EnumRunning(en.put()))) return found;
  Ptr<IBindCtx> bc;
  if (FAILED(CreateBindCtx(0, bc.put()))) return found;
  for (;;) {
    Ptr<IMoniker> mk;
    ULONG got = 0;
    if (en->Next(1, mk.put(), &got) != S_OK || got == 0) break;
    LPOLESTR name = nullptr;
    if (FAILED(mk->GetDisplayName(bc.get(), nullptr, &name)) || !name) continue;
    std::string display = narrow(name, wcslen(name));
    CoTaskMemFree(name);
    auto parsed = parseDteMoniker(display);
    if (!parsed) continue;
    Ptr<IUnknown> unk;
    if (FAILED(rot->GetObject(mk.get(), unk.put())) || !unk) continue;  // stale entry
    IDispatch* d = nullptr;
    if (FAILED(unk->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&d))) || !d) continue;
    DteInstance inst;
    inst.info = *parsed;
    inst.displayName = display;
    inst.dte = Dispatch(d, false);
    found.push_back(std::move(inst));
  }
  std::stable_sort(found.begin(), found.end(),
                   [](const DteInstance& a, const DteInstance& b) { return dteNewer(a.info, b.info); });
  return found;
}

}  // namespace tf::com
