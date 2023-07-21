#pragma once
#include <memory>

// Identical to std::make_unique, but noop if the argument is already a unique_ptr
template <typename T, typename... Args>
auto to_unique_ptr(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}

// Identical to std::make_unique, but noop if the argument is already a unique_ptr
template <typename T>
std::unique_ptr<typename T::element_type> to_unique_ptr(T ptr) {
    return ptr;
}
