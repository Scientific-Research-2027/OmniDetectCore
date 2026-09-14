# OmniDetectCore

OmniDetectCore is a C++20, production-oriented object-detection pipeline for desktop and ARM64 edge devices. It keeps capture, preprocessing, detection, inference runtimes, tracking, temporal monitoring, events, and outputs behind separate interfaces. YOLO26 is the primary detector; NCNN is the preferred Raspberry Pi 5 runtime and ONNX Runtime is the desktop/reference runtime.

The repository intentionally contains no model weights. A backend validates the real exported tensor at runtime instead of assuming one fixed YOLO output shape.

## Architecture

```text
Image / Video / Webcam / Camera ID adapter
                    |
                    v
              IFrameSource
                    |
        Capture worker + bounded latest-frame queue
                    |
                    v
        Preprocessor (resize + letterbox + RGB/NCHW)
                    |
                    v
              IObjectDetector
                    |
                    v
             Yolo26Detector
                    |
                    v
             IInferenceBackend
              /             \
          NCNN            ONNX Runtime
                    |
                    v
        IoU tracker -> temporal ObjectMonitor/KiteMonitor
                    |
                    v
                EventEngine
                    |
       bounded result queue + delivery worker
          /             |              \
      Qt UI         Image/JSON       Recorder / future MQTT
```

Dependency direction is `apps -> application -> core`, with `input` and `output` adapters depending on core contracts. The core library does not include Qt, OpenCV, MQTT, NCNN, or ONNX types in its public interfaces. `Frame` owns an immutable image buffer through `shared_ptr`; copies are cheap and image bytes remain alive across worker/UI handoff. Detection boxes are always expressed as pixels in the original frame.

## Modules

- `core/frame`, `core/detection`: stable data model and detector contract.
- `core/preprocessing`: aspect-ratio-preserving letterbox and reverse coordinate mapping.
- `core/yolo`: export-shape validation, YOLO decode, filtering, class-aware NMS, coordinate restore.
- `core/inference`: generic tensor API, backend registry, NCNN and ONNX Runtime adapters.
- `core/tracking`: optional lightweight class-aware IoU tracking.
- `core/monitoring`: configurable `NONE -> CANDIDATE -> CONFIRMED -> LOST` temporal policy and event bus.
- `input`: image, video, webcam, and protocol-neutral Camera ID source seam.
- `application`: capture/inference/delivery workers, bounded queues, lifecycle, metrics.
- `output`: Qt queued delivery, PPM+JSON image output, quota-aware video recorder, MQTT seam.
- `apps/desktop`: Qt 6 UI. The GUI thread only renders and handles controls.
- `apps/edge`: Qt-free executable with SIGINT/SIGTERM shutdown.

## Dependencies

Required:

- CMake 3.24+
- A C++20 compiler (MSVC 19.3+, GCC 11+, or Clang 14+)
- Threads

Optional:

- OpenCV 4 (`core`, `imgcodecs`, `imgproc`, `videoio`) for real image/video/webcam adapters and recording.
- Qt 6 Widgets for `omnidetect_desktop`.
- NCNN CMake package for the Raspberry Pi/desktop NCNN backend.
- ONNX Runtime C++ CMake package for the ONNX backend.
- spdlog. If absent, the same logging facade uses a thread-safe standard-stream fallback.
- GoogleTest. If absent, the same tests build with the in-repository compatibility runner so an offline core build remains testable.

Dependencies are discovered through their CMake config packages. Use a vcpkg/conan toolchain or set `CMAKE_PREFIX_PATH`, `Qt6_DIR`, `ncnn_DIR`, and `onnxruntime_DIR`. Nothing is downloaded during configure.

## CMake options

| Option | Default | Purpose |
|---|---:|---|
| `OMNIDETECT_BUILD_DESKTOP` | `ON` | Build Qt application when Qt 6 is found |
| `OMNIDETECT_BUILD_EDGE` | `ON` | Build headless application |
| `OMNIDETECT_BUILD_TESTS` | `ON` | Build and register unit tests |
| `OMNIDETECT_BUILD_BENCHMARK` | `ON` | Build portable benchmark |
| `OMNIDETECT_ENABLE_NCNN` | `OFF` | Require and compile NCNN |
| `OMNIDETECT_ENABLE_ONNX` | `OFF` | Require and compile ONNX Runtime |
| `OMNIDETECT_ENABLE_MQTT` | `OFF` | Reserve MQTT transport integration |
| `OMNIDETECT_ENABLE_TRACKING` | `ON` | Compile tracking path |

Enabling NCNN/ONNX without its package is a configure error with an actionable message. Missing Qt automatically disables only the desktop target. Missing OpenCV leaves the portable core, tests, and edge executable buildable; real source adapters then return an explicit runtime error.

## Build on Windows

From an x64 Visual Studio Developer PowerShell/Command Prompt:

```powershell
cmake -S . -B build -A x64 `
  -DOMNIDETECT_ENABLE_ONNX=ON `
  -Donnxruntime_DIR=C:\deps\onnxruntime\lib\cmake\onnxruntime `
  -DQt6_DIR=C:\Qt\6.8.0\msvc2022_64\lib\cmake\Qt6
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

For a dependency-free validation build, add `-DOMNIDETECT_BUILD_DESKTOP=OFF -DOMNIDETECT_ENABLE_ONNX=OFF`.

## Build on Linux

```bash
sudo apt install build-essential cmake libopencv-dev qt6-base-dev libspdlog-dev libgtest-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DOMNIDETECT_ENABLE_ONNX=ON \
  -Donnxruntime_DIR=/opt/onnxruntime/lib/cmake/onnxruntime
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Raspberry Pi 5 (64-bit)

Build NCNN for ARM64 (CPU-only is the safe default), install/export its CMake package, then:

```bash
cmake -S . -B build-pi -DCMAKE_BUILD_TYPE=Release \
  -DOMNIDETECT_BUILD_DESKTOP=OFF \
  -DOMNIDETECT_BUILD_EDGE=ON \
  -DOMNIDETECT_ENABLE_NCNN=ON \
  -DOMNIDETECT_ENABLE_ONNX=OFF \
  -Dncnn_DIR=/opt/ncnn/lib/cmake/ncnn
cmake --build build-pi --parallel 4
ctest --test-dir build-pi --output-on-failure
sudo cmake --install build-pi --prefix /opt/omnidetect
```

Keep `inference.threads` at 3–4 initially and profile temperature/throttling. Vulkan is opt-in and should only be enabled after confirming the Pi image, driver, and NCNN build support it. Frame/result queue defaults are two each, so memory does not grow when inference is slower than capture.

Copy and edit [`deploy/systemd/omnidetect.service`](deploy/systemd/omnidetect.service), install it in `/etc/systemd/system`, then run `sudo systemctl enable --now omnidetect`.

## Model export

Python is tooling only and is not part of the C++ runtime:

```bash
python -m venv .venv
. .venv/bin/activate                 # Windows: .venv\Scripts\activate
pip install -r tools/model_export/requirements.txt
python tools/model_export/export_model.py model.pt --format onnx --imgsz 416 --output models/yolo26n
python tools/model_export/export_model.py model.pt --format ncnn --imgsz 416 --output models/yolo26n
```

The script writes `metadata.json` and `classes.txt` alongside the exported model. Confirm the actual input/output names using the exporter/runtime inspector and set `inference.input_name` plus backend output names when an export does not use `images`/`output0`. `Yolo26Detector` accepts decoded `[N,6]`/`[1,N,6]` (`xyxy, score, class`) and raw center-box class-score matrices in `[1,N,F]` or `[1,F,N]`; incompatible exports fail with the observed shape in the error instead of silently producing bad boxes.

## Configuration and running

Start from [`config/config.yaml`](config/config.yaml). Relative paths resolve against the config file directory.

```bash
# Headless image/video/webcam, selected by source.type in YAML
./build/omnidetect_edge --config config/config.yaml

# Desktop
./build/omnidetect_desktop
```

Desktop workflow: choose **Load Model**, select the backend/input size/confidence and class checkboxes, then **Open Image**, **Open Video**, or **Open Camera**. **Stop** joins all workers and releases the source; **Refresh** clears both queues, resets tracker/monitor state, reopens the same source, and starts fresh workers. A model/backend error is shown before capture starts.

Important configuration keys:

- `model.path`, `input_size` (or `input_width`/`input_height`), `confidence`, `iou`, `max_detections`, `class_names`, `output_layout`.
- `inference.backend`, `threads`, `input_name`, `weights_path`, `vulkan`.
- `source.type`: `image`, `video`, `webcam`, or `camera_id`; plus `path`, `device`, dimensions, FPS, loop, and ID.
- `pipeline.frame_queue_size`, `result_queue_size`, `drop_old_frames`.
- `tracking.enabled`, `minimum_iou`, `max_lost_frames`.
- `monitoring.enabled`, `confirmation_frames`, `lost_frames`, `minimum_confidence`, `target_classes`.
- `output.image_path`, `recorder`, `recording_path`; recorder has a bounded quota in the edge app.

## Threading and shutdown

The capture worker pushes into a bounded queue. At capacity it drops the oldest frame by default, favoring fresh realtime data. Inference owns the detector, tracker, and monitor. A second bounded queue separates inference from output delivery. Condition variables avoid busy waiting. `stop()` atomically requests cancellation, closes both queues, joins capture/inference/delivery workers, releases `VideoCapture`, then stops sinks. No thread is detached.

Temporary capture failures publish `SourceDisconnected`; the first good frame publishes `SourceRecovered`. Fatal source errors and end-of-stream close the pipeline deterministically. Metrics expose capture/inference FPS, drop counts, delivered results, and average latency.

## Extending

- New input: implement `IFrameSource`, keep transport/SDK objects private, then add one factory registration/case. `CameraIdSource` deliberately requires an injected `ICameraIdAdapter`; no undocumented protocol was invented.
- New backend: implement `IInferenceBackend` using generic `Tensor`, register a creator with `BackendFactory`, and add a guarded CMake target/dependency. Runtime-specific types stay in the `.cpp` PIMPL.
- New output: implement `IResultSink` and add it before pipeline start. Sink delivery never runs on the capture or GUI thread.
- New detector: implement `IObjectDetector`; the pipeline, sources, outputs, tracker, and monitor do not change.
- New monitoring policy: configure/derive `ObjectMonitor` and publish its generic events through `EventEngine`.

## Tests and benchmark

```bash
cmake -S . -B build-test -DOMNIDETECT_BUILD_DESKTOP=OFF
cmake --build build-test --parallel
ctest --test-dir build-test --output-on-failure
./build-test/omnidetect_benchmark
```

Unit coverage includes `Frame` copy/move ownership, config parsing and invalid ranges, letterbox/reverse mapping, synthetic YOLO filtering/NMS, latest-frame queue capacity, event snapshot safety, temporal confirmation/loss, backend factory, and mock pipeline start/stop/restart without a camera. The benchmark warms up first and reports average, median, P95, and FPS at 320/416/640; its built-in backend is a no-op, so it measures portable pre/post-processing, not NCNN/ONNX model speed.

## Known limitations

- YOLO26 exports vary. Verify metadata, output layout, and class order for the exact model; end-to-end accuracy needs a known test image and real weights.
- `ObjectTracker` is a lightweight IoU tracker, not a full ByteTrack implementation. Replace it behind the same boundary when occlusion-heavy tracking is required.
- Camera ID transport remains an adapter contract until its SDK/protocol is provided.
- MQTT has a dependency-free seam but no broker client is bundled.
- Recorder quota currently stops recording at the limit; time/file rotation can be added as another sink policy.
- A physical webcam, Raspberry Pi, NCNN model, ONNX model, and Qt desktop require their respective hardware/dependencies and are not covered by portable CI.

See [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md) for dependency licensing notes. Project code is Apache-2.0 licensed.
