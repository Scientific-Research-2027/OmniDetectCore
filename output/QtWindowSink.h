#pragma once

#include "output/IResultSink.h"

#include <QObject>
#include <QMetaType>

Q_DECLARE_METATYPE(omnidetect::Frame)
Q_DECLARE_METATYPE(omnidetect::DetectionResult)

namespace omnidetect {

// Qt-specific adapter: worker calls consume(); Qt delivers resultReady on the GUI event loop.
class QtWindowSink final : public QObject, public IResultSink {
  Q_OBJECT

 public:
  explicit QtWindowSink(QObject* parent = nullptr) : QObject(parent) {}
  void consume(const Frame& frame, const DetectionResult& result) noexcept override;
  [[nodiscard]] std::string name() const override { return "qt-window"; }

 signals:
  void resultReady(omnidetect::Frame frame, omnidetect::DetectionResult result);
};

}  // namespace omnidetect

