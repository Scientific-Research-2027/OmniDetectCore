# Kiến trúc đa camera

## Quyền sở hữu

`EdgeMonitoringService` là composition root của đường chạy edge. Nó sở hữu event bus, scheduler, result router, camera manager và inference service. `InferenceService` sở hữu duy nhất một `IObjectDetector`; detector sở hữu một backend/model. Vì vậy số camera không làm tăng số lần nạp model.

`CameraManager` sở hữu map session theo `cameraId`. Mỗi `CameraSession` sở hữu nguồn hình, tracker IoU, monitor đa đối tượng, queue frame và tùy chọn control worker/Auto‑PTZ. Camera được thêm/xóa/restart độc lập; ID trùng bị từ chối.

## Luồng dữ liệu

1. Capture worker của từng session đọc frame và gắn `cameraId`.
2. Queue giới hạn giữ dữ liệu mới; khi đầy, frame cũ bị bỏ và tăng `framesDropped`.
3. Scheduler weighted round-robin duyệt camera theo `priority` (1–16), chỉ lấy frame mới nhất, tôn trọng `inference_fps` và loại frame quá `max_frame_age_ms`.
4. Một inference worker gọi detector tuần tự. `worker_count` hiện buộc bằng 1 để không giả định backend thread-safe.
5. Result router nhận `(session, generation, frame, result)`. Generation sai bị loại sau reconnect/stop/remove nên kết quả không thể đi sang phiên camera mới.
6. Tracker và monitor chạy theo session. Monitor lưu trạng thái theo `(cameraId, trackId)` và sinh `Detected/Confirmed/Lost` cho từng vật thể.
7. Auto‑PTZ chỉ nhận detection đã tracking. Lệnh đi qua control worker riêng, không chặn inference.
8. Sink chạy sau queue kết quả; lỗi sink không làm dừng capture/inference.

## Đồng bộ và shutdown

Queue đều hữu hạn. Worker chờ bằng condition variable, không polling nóng và không có detached thread. Callback/event được gọi từ snapshot subscriber, không giữ mutex nội bộ của event bus. Detector chỉ được gọi từ inference worker.

Thứ tự dừng của service là camera → inference → result router. Session tăng generation trước khi join capture và reset tracker/monitor. Result đang bay có generation cũ sẽ bị từ chối. Control worker gửi STOP cuối cùng trước khi đóng adapter.

Một hạn chế thực tế: một số backend `VideoCapture` của bên thứ ba có thể không hủy ngay một lệnh read đang kẹt. RTSP adapter đặt open/read timeout; driver webcam/libcamera vẫn phải được kiểm tra trên nền tảng đích.

## Hướng mở rộng

- Nguồn mới: triển khai `IFrameSource`, giữ handle SDK trong PIMPL và đăng ký ở `SourceFactory`.
- Backend mới: triển khai `IInferenceBackend`; không đưa type runtime vào public API.
- Tracker mới: triển khai `IObjectTracker` và inject qua `CameraManager::TrackerFactory`.
- Policy monitor mới: triển khai `IMonitorPolicy`; không sửa scheduler hoặc detector.
- Control mới: triển khai `ICameraControl`; mọi lệnh vẫn đi qua `CameraControlWorker`.
- Output mới: triển khai `IResultSink`, thêm trước khi service start.
