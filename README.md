# OmniDetectCore

OmniDetectCore là lõi giám sát hình ảnh AI đa camera viết bằng C++20, hướng tới Raspberry Pi 5 64-bit và vẫn giữ ứng dụng desktop để kiểm thử/tích hợp. Một tiến trình chỉ khởi tạo **một detector YOLO26** và **một backend/model dùng chung**; mỗi camera có capture worker, queue latest-frame giới hạn, tracker, monitor và trạng thái reconnect riêng.

Không có số liệu hiệu năng phần cứng giả định trong kho mã. Kết quả NCNN/ONNX, RTSP, CSI/libcamera, ONVIF và V4L2 phải được xác nhận trên đúng model, camera và hệ điều hành đích.

## Kiến trúc

```text
RTSP / libcamera / V4L2 / video / ảnh
                 │
        CameraManager (N camera)
                 │
       CameraSession cho từng camera
    capture + queue latest + reconnect
                 │
      RoundRobinInferenceScheduler
          stale-frame / FPS budget
                 │
    InferenceService: 1 worker mặc định
       1 Yolo26Detector + 1 backend
                 │
       ResultRouter bất đồng bộ
          ┌──────┼────────┐
     tracker   monitor   Auto-PTZ
      riêng      riêng    riêng
          └──────┼────────┘
       output + EventEngine + metrics
```

Đường single-camera cũ (`DetectionPipeline`) vẫn được giữ để tương thích với desktop và cấu hình cũ. Khi YAML có `cameras:`, `omnidetect_edge` tự động dùng kiến trúc đa camera mới.

## Điểm chính

- Round-robin lấy frame mới nhất, loại frame quá tuổi và không để camera FPS cao chiếm toàn bộ inference.
- Một detector dùng chung, mặc định một inference worker; cấu hình từ chối `worker_count != 1` cho tới khi backend được chứng minh thread-safe.
- Lỗi/reconnect một camera không dừng camera khác; generation ngăn kết quả cũ đi nhầm phiên.
- Tracker IoU và monitor theo khóa `(cameraId, trackId)`, không hard-code lớp đối tượng.
- RTSP qua OpenCV/FFmpeg; libcamera qua OpenCV/GStreamer khi bật tùy chọn Linux.
- Điều khiển có queue giới hạn, STOP ưu tiên, rate limit, manual override, ONVIF tùy chọn qua libcurl và V4L2 trên Linux.
- Auto‑PTZ dùng tâm bbox, dead-zone/hysteresis, giới hạn tốc độ, zoom theo tỷ lệ bbox và target lock.
- Metrics theo camera và toàn cục: FPS, drop/skip/stale, reconnect, latency trung bình/P95, queue, utilization và model state.
- Không detached thread; shutdown đóng queue, đánh thức condition variable và join worker theo thứ tự.

## Phụ thuộc và tùy chọn CMake

Yêu cầu tối thiểu: CMake 3.24, compiler C++20 và Threads. OpenCV, Qt 6, NCNN, ONNX Runtime, spdlog, GoogleTest và libcurl là tùy chọn; CMake không tự tải dependency.

| Tùy chọn | Mặc định | Mục đích |
|---|---:|---|
| `OMNIDETECT_BUILD_DESKTOP` | `ON` | Build giao diện Qt nếu tìm thấy Qt 6 |
| `OMNIDETECT_BUILD_EDGE` | `ON` | Build ứng dụng edge headless |
| `OMNIDETECT_BUILD_TESTS` | `ON` | Build kiểm thử |
| `OMNIDETECT_BUILD_BENCHMARK` | `ON` | Build benchmark portable |
| `OMNIDETECT_ENABLE_NCNN` | `OFF` | Backend ưu tiên cho Pi |
| `OMNIDETECT_ENABLE_ONNX` | `OFF` | Backend ONNX Runtime |
| `OMNIDETECT_ENABLE_LIBCAMERA` | `OFF` | Adapter libcamera/GStreamer trên Linux |
| `OMNIDETECT_ENABLE_ONVIF` | `OFF` | Gửi PTZ ONVIF qua libcurl |
| `OMNIDETECT_ENABLE_TRACKING` | `ON` | Tracker IoU tích hợp |
| `OMNIDETECT_WARNINGS_AS_ERRORS` | `OFF` | Xem warning của dự án là lỗi |

## Build nhanh

Portable, không cần runtime AI hay Qt:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DOMNIDETECT_BUILD_DESKTOP=OFF \
  -DOMNIDETECT_ENABLE_NCNN=OFF \
  -DOMNIDETECT_ENABLE_ONNX=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Raspberry Pi 5/ARM64 với NCNN CPU và RTSP:

```bash
sudo apt update
sudo apt install build-essential cmake libopencv-dev libspdlog-dev
cmake -S . -B build-pi -DCMAKE_BUILD_TYPE=Release \
  -DOMNIDETECT_BUILD_DESKTOP=OFF \
  -DOMNIDETECT_ENABLE_NCNN=ON \
  -DOMNIDETECT_ENABLE_ONNX=OFF \
  -Dncnn_DIR=/opt/ncnn/lib/cmake/ncnn
cmake --build build-pi --parallel 4
ctest --test-dir build-pi --output-on-failure
```

CSI/libcamera cần OpenCV có GStreamer và `-DOMNIDETECT_ENABLE_LIBCAMERA=ON`. ONVIF cần libcurl development và `-DOMNIDETECT_ENABLE_ONVIF=ON`.

Windows với ONNX Runtime binary distribution:

```powershell
cmake -S . -B build -A x64 `
  -DOMNIDETECT_ENABLE_ONNX=ON `
  -DONNXRUNTIME_ROOT=C:\deps\onnxruntime
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Các preset mẫu nằm trong [CMakePresets.json](CMakePresets.json).

## Cấu hình và chạy

Sao chép [config.example.yaml](config/config.example.yaml) thành file triển khai riêng. Không ghi username/password hoặc RTSP URI thật vào Git; dùng `${TÊN_BIẾN_MÔI_TRƯỜNG}`.

```bash
export CAM_ENTRANCE_RTSP='rtsp://user:password@192.0.2.10/stream'
export CAM_ENTRANCE_ONVIF_ENDPOINT='http://192.0.2.10/onvif/ptz_service'
export CAM_ENTRANCE_ONVIF_USER='operator'
export CAM_ENTRANCE_ONVIF_PASSWORD='secret'
./build-pi/omnidetect_edge --config /etc/omnidetect/config.yaml
```

`Ctrl+C`/`SIGTERM` dừng có thứ tự. Image sink tạo tên có `cameraId` và `frameId`; recorder tạo file riêng cho từng camera để không trộn luồng.

## Model YOLO26

Kho mã không tự tải model. ONNX thường dùng một file; NCNN cần `.param` ở `model.path` và `.bin` ở `model.weights_path`. Lưu `metadata.json`/`classes.txt`, kiểm tra input/output name, kích thước và thứ tự class.

```bash
python tools/model_export/export_model.py model.pt --format onnx --imgsz 416 --output models/yolo26n
python tools/model_export/export_model.py model.pt --format ncnn --imgsz 416 --output models/yolo26n
```

Detector chấp nhận output đã decode `[N,6]`/`[1,N,6]` hoặc ma trận class-score `[1,N,F]`/`[1,F,N]`. Shape không tương thích trả lỗi rõ ràng.

## Tài liệu

- [Kiến trúc và luồng dữ liệu](docs/architecture.md)
- [Cấu hình đa camera](docs/configuration.md)
- [Vận hành Raspberry Pi và systemd](docs/operations.md)
- [Hiệu năng, overload và soak test](docs/performance.md)
- [Chiến lược kiểm thử](docs/testing.md)
- [Ghi chú migration](CHANGELOG.md)
- [Giấy phép dependency](THIRD_PARTY_LICENSES.md)

## Giới hạn đã biết

- Chưa có benchmark Pi trong kho mã; phải chạy benchmark/soak trên thiết bị thật.
- OpenCV phải có FFmpeg cho RTSP và GStreamer/libcamerasrc cho libcamera.
- ONVIF dùng endpoint/profile token cấu hình; chưa tự discovery/range. Continuous move, stop và home được gửi khi camera hỗ trợ.
- V4L2 PTZ phụ thuộc control ID của driver; camera không có control phù hợp sẽ báo lỗi.
- Tracker IoU nhẹ không thay thế ByteTrack trong cảnh che khuất nặng.
- MQTT giữ seam kiến trúc nhưng chưa đóng gói broker client.

Mã dự án dùng Apache-2.0; model/dataset và dependency giữ giấy phép riêng.
