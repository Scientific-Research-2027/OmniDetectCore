#include "apps/desktop/MainWindow.h"
#include "core/detection/DetectionResult.h"
#include "core/frame/Frame.h"

#include <QApplication>
#include <QMetaType>

int main(int argc, char** argv) {
  QApplication application(argc, argv);
  qRegisterMetaType<omnidetect::Frame>("omnidetect::Frame");
  qRegisterMetaType<omnidetect::DetectionResult>("omnidetect::DetectionResult");
  omnidetect::MainWindow window;
  window.show();
  return application.exec();
}
