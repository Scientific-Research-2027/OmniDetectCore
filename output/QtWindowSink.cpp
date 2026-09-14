#include "output/QtWindowSink.h"

namespace omnidetect {

void QtWindowSink::consume(const Frame& frame, const DetectionResult& result) noexcept {
  emit resultReady(frame, result);
}

}  // namespace omnidetect

