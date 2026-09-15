# Lịch sử thay đổi

## Chưa phát hành

### Kiến trúc đa camera

- Thêm `CameraManager`/`CameraSession`, weighted round-robin latest-frame scheduler, shared inference service và result router.
- Chỉ một detector/backend/model cho nhiều camera; tracker và monitor tách theo camera.
- Thêm reconnect/backoff, generation chống stale result, metrics theo camera/toàn cục.
- Thêm RTSP và adapter libcamera tùy chọn.
- Thêm interface điều khiển, ONVIF/libcurl, V4L2, control worker và Auto‑PTZ.
- Recorder/image output gắn camera ID; recorder tách file theo camera.
- Thêm cấu hình, tài liệu tiếng Việt, preset và công cụ soak.

### Migration từ single-camera

Cấu hình cũ có `source:` tiếp tục chạy bằng `DetectionPipeline`. Để chuyển sang shared inference, thay bằng danh sách `cameras:` và bổ sung `inference.worker_count`, `scheduler`, `max_frame_age_ms`. Các khóa `tracking`/`monitoring` toàn cục cũ chuyển vào từng camera. Xem [config.example.yaml](config/config.example.yaml).

Ứng dụng desktop vẫn dùng pipeline cũ trong giai đoạn này; executable edge tự chọn đường mới khi có `cameras:`.
