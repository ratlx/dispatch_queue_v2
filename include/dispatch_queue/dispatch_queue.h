#pragma once

#include <exception>
#include <memory>

#include "detail/dq_base.h"
#include "detail/dq_executor.h"
#include "detail/utils.h"
#include "dispatch_group.h"

namespace Rat {

inline std::shared_ptr<Rat::DispatchQueue> CreateQueue(
    bool is_serial) noexcept {
  auto PrintExceptInfo = [](std::string_view info) noexcept {
    std::cerr << "Exception occur while creating dispatch queue : " << info
              << std::endl;
  };

  try {
    auto queue = std::make_shared<DispatchQueue>(is_serial);
    return queue;
  } catch (const std::exception& ex) {
    PrintExceptInfo(ex.what());
  } catch (...) {
    PrintExceptInfo("unknown error");
  }

  return nullptr;
}

}  // namespace Rat