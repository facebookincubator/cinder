#ifndef __STRICTM_CALLABLE_WRAPPER_H__
#define __STRICTM_CALLABLE_WRAPPER_H__
#include "StrictModules/Objects/instance.h"
#include "StrictModules/Objects/object_type.h"

#include "StrictModules/caller_context.h"
#include "StrictModules/caller_context_impl.h"

namespace strictmod::objects {

// function pointer of a wrapper function
template <typename T, typename... Args>
using WrappedFType = std::shared_ptr<BaseStrictObject> (*)(
    std::shared_ptr<T>,
    const CallerContext&,
    Args...);

template <typename T, typename... Args>
class CallableWrapper {
  static_assert(
      std::is_base_of<BaseStrictObject, T>::value,
      "instance type of wrapper function must be strict object");

 public:
  CallableWrapper(WrappedFType<T, Args...> func, std::string name)
      : func_(func), name_(name) {}

  std::shared_ptr<BaseStrictObject> operator()(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const std::vector<std::string>& namedArgs,
      const CallerContext& caller) {
    if (!namedArgs.empty()) {
      throw std::runtime_error("named arguments in builtin call not supported");
    }
    const int n = sizeof...(Args);
    if (n != args.size()) {
      caller.raiseTypeError(
          "{}() takes {} positional arguments but {} were given",
          name_,
          n,
          args.size());
    }
    return callStatic(
        std::move(obj), args, caller, std::make_index_sequence<n>());
  }

 private:
  WrappedFType<T, Args...> func_;
  std::string name_;

  template <size_t... Is>
  std::shared_ptr<BaseStrictObject> callStatic(
      std::shared_ptr<BaseStrictObject> obj,
      const std::vector<std::shared_ptr<BaseStrictObject>>& args,
      const CallerContext& caller,
      std::index_sequence<Is...>) {
    return func_(
        std::static_pointer_cast<T>(std::move(obj)), caller, args[Is]...);
  }
};

} // namespace strictmod::objects

#endif // __STRICTM_CALLABLE_WRAPPER_H__
