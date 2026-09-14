#include "apps/desktop/MainWindow.h"

#include "application/DetectionPipeline.h"
#include "core/inference/BackendFactory.h"
#include "core/yolo/Yolo26Detector.h"
#include "input/SourceFactory.h"
#include "output/QtWindowSink.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <fstream>
#include <algorithm>
#include <string>
#include <thread>

namespace omnidetect {
namespace {

std::filesystem::path filesystemPath(const QString& value) {
#ifdef _WIN32
  return std::filesystem::path(value.toStdWString());
#else
  return std::filesystem::path(value.toUtf8().constData());
#endif
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  buildUi();
  setWindowTitle("OmniDetectCore Desktop");
  resize(1200, 780);
  metricsTimer_ = new QTimer(this);
  metricsTimer_->setInterval(500);
  connect(metricsTimer_, &QTimer::timeout, this, &MainWindow::updateMetrics);
  metricsTimer_->start();
  setRunningUi(false);
}

MainWindow::~MainWindow() { controller_.stop(); }

void MainWindow::buildUi() {
  auto* central = new QWidget(this);
  auto* root = new QHBoxLayout(central);
  auto* controls = new QVBoxLayout();
  auto* buttons = new QGroupBox("Source", central);
  auto* buttonLayout = new QVBoxLayout(buttons);
  loadModelButton_ = new QPushButton("Load Model", buttons);
  openImageButton_ = new QPushButton("Open Image", buttons);
  openVideoButton_ = new QPushButton("Open Video", buttons);
  openCameraButton_ = new QPushButton("Open Camera", buttons);
  stopButton_ = new QPushButton("Stop", buttons);
  refreshButton_ = new QPushButton("Refresh", buttons);
  for (auto* button : {loadModelButton_, openImageButton_, openVideoButton_, openCameraButton_, stopButton_, refreshButton_}) {
    buttonLayout->addWidget(button);
  }

  auto* settings = new QGroupBox("Detection", central);
  auto* form = new QFormLayout(settings);
  backendBox_ = new QComboBox(settings);
  backendBox_->addItems({"onnx", "ncnn"});
  inputSizeBox_ = new QSpinBox(settings);
  inputSizeBox_->setRange(32, 2048);
  inputSizeBox_->setSingleStep(32);
  inputSizeBox_->setValue(416);
  confidenceBox_ = new QDoubleSpinBox(settings);
  confidenceBox_->setRange(0.01, 1.0);
  confidenceBox_->setSingleStep(0.05);
  confidenceBox_->setValue(0.45);
  cameraBox_ = new QSpinBox(settings);
  cameraBox_->setRange(0, 32);
  classList_ = new QListWidget(settings);
  classList_->setMinimumHeight(130);
  form->addRow("Backend", backendBox_);
  form->addRow("Input size", inputSizeBox_);
  form->addRow("Confidence", confidenceBox_);
  form->addRow("Camera index", cameraBox_);
  form->addRow("Class filter", classList_);
  controls->addWidget(buttons);
  controls->addWidget(settings);
  controls->addStretch();

  auto* display = new QVBoxLayout();
  imageLabel_ = new QLabel("Load a model, then open an image, video, or camera.", central);
  imageLabel_->setAlignment(Qt::AlignCenter);
  imageLabel_->setMinimumSize(640, 480);
  imageLabel_->setStyleSheet("QLabel { background: #181818; color: #dddddd; }");
  imageLabel_->setScaledContents(false);
  statusLabel_ = new QLabel("Model: not selected", central);
  metricsLabel_ = new QLabel("FPS: 0 | latency: 0 ms", central);
  display->addWidget(imageLabel_, 1);
  display->addWidget(statusLabel_);
  display->addWidget(metricsLabel_);
  root->addLayout(controls);
  root->addLayout(display, 1);
  setCentralWidget(central);

  connect(loadModelButton_, &QPushButton::clicked, this, &MainWindow::chooseModel);
  connect(openImageButton_, &QPushButton::clicked, this, &MainWindow::openImage);
  connect(openVideoButton_, &QPushButton::clicked, this, &MainWindow::openVideo);
  connect(openCameraButton_, &QPushButton::clicked, this, &MainWindow::openCamera);
  connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopPipeline);
  connect(refreshButton_, &QPushButton::clicked, this, &MainWindow::refreshPipeline);
}

void MainWindow::chooseModel() {
  const auto file = QFileDialog::getOpenFileName(this, "Select model", {},
                                                 "Inference models (*.onnx *.param);;All files (*)");
  if (file.isEmpty()) return;
  modelPath_ = filesystemPath(file);
  if (modelPath_.extension() == ".onnx") backendBox_->setCurrentText("onnx");
  else if (modelPath_.extension() == ".param") backendBox_->setCurrentText("ncnn");
  loadClassNames();
  statusLabel_->setText("Model selected: " + file + " (loaded when a source starts)");
}

void MainWindow::loadClassNames() {
  classList_->clear();
  if (modelPath_.empty()) return;
  auto namesPath = modelPath_.parent_path() / "classes.txt";
  if (!std::filesystem::exists(namesPath)) {
    namesPath = modelPath_;
    namesPath.replace_extension(".names");
  }
  std::ifstream input(namesPath);
  std::string name;
  while (std::getline(input, name)) {
    if (name.empty()) continue;
    auto* item = new QListWidgetItem(QString::fromStdString(name), classList_);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Checked);
  }
}

void MainWindow::openImage() {
  const auto file = QFileDialog::getOpenFileName(this, "Open image", {}, "Images (*.png *.jpg *.jpeg *.bmp *.webp)");
  if (file.isEmpty()) return;
  SourceConfig source;
  source.type = SourceType::Image;
  source.path = filesystemPath(file);
  source.sourceId = "desktop-image";
  static_cast<void>(startSource(std::move(source)));
}

void MainWindow::openVideo() {
  const auto file = QFileDialog::getOpenFileName(this, "Open video", {}, "Videos (*.mp4 *.avi *.mkv *.mov);;All files (*)");
  if (file.isEmpty()) return;
  SourceConfig source;
  source.type = SourceType::Video;
  source.path = filesystemPath(file);
  source.sourceId = "desktop-video";
  static_cast<void>(startSource(std::move(source)));
}

void MainWindow::openCamera() {
  SourceConfig source;
  source.type = SourceType::Webcam;
  source.device = cameraBox_->value();
  source.sourceId = "desktop-camera-" + std::to_string(source.device);
  static_cast<void>(startSource(std::move(source)));
}

bool MainWindow::startSource(SourceConfig sourceConfig) {
  if (modelPath_.empty()) {
    QMessageBox::warning(this, "Model required", "Select an ONNX or NCNN model before starting a source.");
    return false;
  }
  controller_.stop();
  auto backend = BackendFactory::instance().create(backendBox_->currentText().toStdString());
  if (!backend) {
    QMessageBox::critical(this, "Backend error", "The selected backend is unknown.");
    return false;
  }
  DetectorConfig detectorConfig;
  detectorConfig.modelPath = modelPath_;
  detectorConfig.inputWidth = inputSizeBox_->value();
  detectorConfig.inputHeight = inputSizeBox_->value();
  detectorConfig.confidenceThreshold = static_cast<float>(confidenceBox_->value());
  bool allChecked = true;
  for (int index = 0; index < classList_->count(); ++index) {
    const auto* item = classList_->item(index);
    detectorConfig.classNames.push_back(item->text().toStdString());
    if (item->checkState() != Qt::Checked) allChecked = false;
  }
  if (!allChecked) {
    for (int index = 0; index < classList_->count(); ++index) {
      if (classList_->item(index)->checkState() == Qt::Checked) detectorConfig.classWhitelist.insert(index);
    }
  }
  BackendConfig backendConfig;
  backendConfig.modelPath = modelPath_;
  backendConfig.threadCount = static_cast<int>(std::max(1U, std::thread::hardware_concurrency() / 2U));
  auto detector = std::make_unique<Yolo26Detector>(detectorConfig, std::move(backend), backendConfig);
  if (!detector->isReady()) {
    QMessageBox::critical(this, "Model load failed", QString::fromStdString(detector->initializationError()));
    return false;
  }
  std::string error;
  auto source = createFrameSource(sourceConfig, error);
  if (!source) {
    QMessageBox::critical(this, "Source error", QString::fromStdString(error));
    return false;
  }
  PipelineConfig pipelineConfig;
  pipelineConfig.frameQueueSize = 2;
  pipelineConfig.resultQueueSize = 2;
  auto pipeline = std::make_unique<DetectionPipeline>(std::move(source), std::move(detector), pipelineConfig);
  windowSink_ = std::make_shared<QtWindowSink>();
  connect(windowSink_.get(), &QtWindowSink::resultReady, this, &MainWindow::showResult, Qt::QueuedConnection);
  if (!pipeline->addSink(windowSink_, error)) {
    QMessageBox::critical(this, "Output error", QString::fromStdString(error));
    return false;
  }
  pipeline->events().subscribe([this](const Event& event) {
    const auto text = QString::fromStdString(event.message);
    QMetaObject::invokeMethod(this, [this, text] { statusLabel_->setText(text); }, Qt::QueuedConnection);
  });
  if (!controller_.setPipeline(std::move(pipeline), error) || !controller_.start(error)) {
    QMessageBox::critical(this, "Pipeline error", QString::fromStdString(error));
    setRunningUi(false);
    return false;
  }
  lastSource_ = std::move(sourceConfig);
  hasSource_ = true;
  setRunningUi(true);
  statusLabel_->setText("Running " + backendBox_->currentText() + " / " + QString::fromStdString(lastSource_.sourceId));
  return true;
}

void MainWindow::stopPipeline() {
  controller_.stop();
  setRunningUi(false);
  statusLabel_->setText("Stopped");
}

void MainWindow::refreshPipeline() {
  if (!hasSource_) return;
  std::string error;
  if (!controller_.refresh(error)) {
    QMessageBox::critical(this, "Refresh failed", QString::fromStdString(error));
    setRunningUi(false);
    return;
  }
  setRunningUi(true);
  statusLabel_->setText("Source refreshed");
}

void MainWindow::showResult(Frame frame, DetectionResult result) {
  if (!frame.valid()) return;
  const auto& image = *frame.image;
  QImage borrowed;
  if (image.channels == 1) {
    borrowed = QImage(image.bytes.data(), image.width, image.height, static_cast<qsizetype>(image.stride), QImage::Format_Grayscale8);
  } else {
    const auto format = image.format == PixelFormat::Rgb8 ? QImage::Format_RGB888 : QImage::Format_BGR888;
    borrowed = QImage(image.bytes.data(), image.width, image.height, static_cast<qsizetype>(image.stride), format);
  }
  QImage rendered = borrowed.copy();
  QPainter painter(&rendered);
  painter.setRenderHint(QPainter::Antialiasing);
  QPen pen(QColor(255, 90, 50));
  pen.setWidth(2);
  painter.setPen(pen);
  for (const auto& detection : result.detections) {
    const QRectF rectangle(detection.bbox.x, detection.bbox.y, detection.bbox.width, detection.bbox.height);
    painter.drawRect(rectangle);
    const auto track = detection.trackId ? QString(" #%1").arg(*detection.trackId) : QString();
    painter.drawText(rectangle.topLeft() + QPointF(2, -4),
                     QString("%1 %2%3").arg(QString::fromStdString(detection.className))
                                           .arg(detection.confidence, 0, 'f', 2).arg(track));
  }
  painter.end();
  imageLabel_->setPixmap(QPixmap::fromImage(rendered).scaled(imageLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
  if (!result.ok()) statusLabel_->setText(QString::fromStdString(result.error));
}

void MainWindow::updateMetrics() {
  const auto metrics = controller_.metrics();
  metricsLabel_->setText(QString("capture %1 FPS | inference %2 FPS | avg latency %3 ms | dropped %4")
                             .arg(metrics.captureFps, 0, 'f', 1).arg(metrics.inferenceFps, 0, 'f', 1)
                             .arg(metrics.averageLatencyMs, 0, 'f', 2).arg(metrics.droppedFrames));
  if (!controller_.isRunning() && stopButton_->isEnabled()) setRunningUi(false);
}

void MainWindow::setRunningUi(const bool running) {
  stopButton_->setEnabled(running);
  refreshButton_->setEnabled(hasSource_ && !running);
  openImageButton_->setEnabled(!running);
  openVideoButton_->setEnabled(!running);
  openCameraButton_->setEnabled(!running);
  loadModelButton_->setEnabled(!running);
  backendBox_->setEnabled(!running);
  inputSizeBox_->setEnabled(!running);
  confidenceBox_->setEnabled(!running);
  cameraBox_->setEnabled(!running);
  classList_->setEnabled(!running);
}

}  // namespace omnidetect
