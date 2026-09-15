#include "apps/desktop/MainWindow.h"

#include <QApplication>

int main(int argc, char** argv) {
  QApplication application(argc, argv);

  omnidetect::MainWindow window;
  window.show();

  return application.exec();
}