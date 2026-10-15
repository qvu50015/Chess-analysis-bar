#pragma once
#include <functional>
#include <stdexcept>
namespace chess_eval {
using CancelCheck = std::function<bool()>;
class AnalysisCancelled : public std::runtime_error {
public:
  AnalysisCancelled() : std::runtime_error("Analysis stopped") {}
};
inline void check_cancelled(const CancelCheck &cancel) {
  if (cancel && cancel())
    throw AnalysisCancelled();
}
} // namespace chess_eval
