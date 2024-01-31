// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/error_sink.h"

namespace strictmod {
bool BaseErrorSink::hasError() const {
  return !errors_.empty();
}

int BaseErrorSink::getErrorCount() const {
  return errors_.size();
}

const std::vector<std::unique_ptr<StrictModuleException>>&
BaseErrorSink::getErrors() const {
  return errors_;
}

// ErrorSink
std::unique_ptr<BaseErrorSink> ErrorSink::getNestedSink() {
  return std::make_unique<ErrorSink>();
}

void ErrorSink::processError(std::unique_ptr<StrictModuleException> exc) {
  if (errors_.empty()) {
    errors_.push_back(std::move(exc));
  } else {
    errors_[0] = std::move(exc);
  }
  errors_[0]->raise();
}

// Collecting ErrorSink
std::unique_ptr<BaseErrorSink> CollectingErrorSink::getNestedSink() {
  // in nested scope, do not collect and eagerly throw exception
  return std::make_unique<ErrorSink>();
}

void CollectingErrorSink::processError(
    std::unique_ptr<StrictModuleException> exc) {
  errors_.push_back(std::move(exc));
}

} // namespace strictmod
