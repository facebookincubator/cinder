// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_OBJ_HELPER_H__
#define __STRICTM_OBJ_HELPER_H__
#include <memory>

namespace strictmod::objects {
template <typename T, typename V>
inline std::shared_ptr<T> assertStaticCast(std::shared_ptr<V> obj) {
  assert(std::dynamic_pointer_cast<T>(obj) != nullptr);
  return std::static_pointer_cast<T>(obj);
}

inline int normalizeIndex(int index, int size) {
  return index < 0 ? index + size : index;
}
} // namespace strictmod::objects
#endif //__STRICTM_OBJ_HELPER_H__
