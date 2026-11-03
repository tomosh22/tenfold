#pragma once
namespace svc::util {

enum Customer {
    Customer_Auto,
    Customer_Last,
    Customer_Green,
};

enum Queue {
    Queue_Red,
    Queue_Max,
    Queue_Auto,
};

constexpr unsigned kBase8 = 7u;
constexpr long kBase9 = 2L;

using Count10 = long;
using Handle11 = double;

}  // namespace svc::util
