#pragma once

#include "application/AppController.h"
#include "config/ConfigManager.h"

#include <QMainWindow>

#include <filesystem>
#include <memory>

class QLabel;
class QPushButton;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QListWidget;
class QTimer;

namespace omnidetect {

class QtWindowSink;

class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

 private slots:
  void chooseModel();
  void openImage();
  void openVideo();
  void openCamera();
  void stopPipeline();
  void refreshPipeline();
  void showResult(omnidetect::Frame frame, omnidetect::DetectionResult result);
  void updateMetrics();

 private:
  AppController controller_;
  std::shared_ptr<QtWindowSink> windowSink_;
  std::filesystem::path modelPath_;
  SourceConfig lastSource_;
  bool hasSource_{false};

  QPushButton* loadModelButton_{nullptr};
  QPushButton* openImageButton_{nullptr};
  QPushButton* openVideoButton_{nullptr};
  QPushButton* openCameraButton_{nullptr};
  QPushButton* stopButton_{nullptr};
  QPushButton* refreshButton_{nullptr};
  QComboBox* backendBox_{nullptr};
  QSpinBox* inputSizeBox_{nullptr};
  QDoubleSpinBox* confidenceBox_{nullptr};
  QSpinBox* cameraBox_{nullptr};
  QListWidget* classList_{nullptr};
  QLabel* imageLabel_{nullptr};
  QLabel* statusLabel_{nullptr};
  QLabel* metricsLabel_{nullptr};
  QTimer* metricsTimer_{nullptr};

  void buildUi();
  bool startSource(SourceConfig source);
  void loadClassNames();
  void setRunningUi(bool running);
};

}  // namespace omnidetect

