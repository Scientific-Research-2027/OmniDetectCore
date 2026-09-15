# Cấu hình đa camera

Parser hỗ trợ tập YAML cần thiết cho file mẫu, với indentation hai dấu cách. Mỗi phần tử trong `cameras` phải bắt đầu bằng `- id:`. ID phải duy nhất.

## Inference dùng chung

```yaml
inference:
  backend: ncnn
  threads: 4
  worker_count: 1
  scheduler: round_robin
  max_frame_age_ms: 500
  max_total_fps: 12
```

- `threads` là số thread nội bộ backend.
- `worker_count` hiện chỉ nhận `1`.
- `max_total_fps: 0` nghĩa là không đặt trần toàn cục.
- `max_frame_age_ms` loại frame cũ trước detector.

## Camera

`source.type` nhận `image`, `video`, `webcam`/`v4l2`, `rtsp`, `libcamera`/`csi` hoặc `camera_id`. `capture_fps: 0` không throttle ở tầng session; `inference_fps` luôn phải dương. Queue frame chỉ nhận 1–8, khuyến nghị 1 cho realtime.

`priority` nằm trong 1–16. Scheduler cho camera trọng số cao nhiều slot hơn nhưng vẫn thăm camera trọng số thấp trong mỗi vòng. Hãy dùng `inference_fps` để đặt ngân sách chính; priority chỉ dùng cho camera quan trọng hơn khi hệ thống quá tải.

Reconnect dùng exponential backoff từ `reconnect_initial_ms` tới `reconnect_max_ms`. `startup_timeout_ms` là thời gian chờ lần kết nối đầu khi bật fail-fast. `application.fail_fast: false` giữ các camera còn lại; `true` rollback toàn service nếu lần kết nối đầu của bất kỳ camera nào thất bại/hết hạn.

## Tracker và monitor

Tracker mặc định ghép IoU theo class. `minimum_hits` quyết định số hit trước khi công bố `trackId`; `max_lost_frames` xóa track mất lâu.

Monitor lọc theo `target_class_ids` và/hoặc `target_classes`. Nếu cả hai rỗng, mọi class đạt `minimum_confidence` đều được theo dõi. `confirmation_frames` và `lost_frames` tránh event nhấp nháy.

## RTSP và secret

Không đưa credential thật vào YAML được commit. Các trường chuỗi trong camera source/control hỗ trợ `${ENV_NAME}` và fail rõ nếu biến thiếu. Lỗi adapter RTSP/ONVIF chỉ ghi camera ID, không ghi URI/password.

```yaml
source:
  type: rtsp
  uri: ${CAM_1_RTSP}
```

Nên cấp tài khoản camera chỉ có quyền cần thiết, giới hạn quyền đọc stream/PTZ, và bảo vệ file environment của systemd bằng permission hệ điều hành.

## ONVIF, V4L2 và Auto‑PTZ

ONVIF cần endpoint PTZ service và profile token đúng camera. Bản build phải bật `OMNIDETECT_ENABLE_ONVIF`. V4L2 dùng `control.endpoint: /dev/videoN` hoặc fallback về `source.device` dạng đường dẫn.

`manual_override_ms` chặn lệnh tự động sau lệnh tay. Auto‑PTZ dùng:

- `dead_zone`/`hysteresis`: tránh rung quanh tâm;
- `gain`/`max_speed`: chuyển sai số tâm thành tốc độ giới hạn;
- `target_box_ratio`/`zoom_dead_band`: giữ kích thước mục tiêu;
- `target_hold_ms`, `lost_timeout_ms`, `switch_margin`: ổn định chọn mục tiêu;
- `priority_classes`: thứ tự class ưu tiên.

Không bật Auto‑PTZ trước khi kiểm tra chiều pan/tilt/zoom trên camera thật ở tốc độ thấp.

File đầy đủ: [config/config.example.yaml](../config/config.example.yaml).
