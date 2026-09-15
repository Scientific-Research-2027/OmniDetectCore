# Hiệu năng và soak test

## Mô hình ngân sách

Với latency inference P95 là `L` giây, một worker có trần lý thuyết khoảng `1/L` FPS trước chi phí capture, preprocessing, postprocessing và output. Tổng `inference_fps` camera nên thấp hơn năng lực đo được để còn headroom nhiệt/IO. Queue latest-frame giữ latency thấp bằng cách bỏ công việc cũ, không cố xử lý mọi frame.

Khi overload, hệ thống ưu tiên theo thứ tự:

1. giữ process và camera session còn sống;
2. xử lý frame mới thay vì backlog;
3. bảo đảm weighted round-robin vẫn thăm từng camera;
4. ghi nhận drop/skip/stale để vận hành biết đang mất mẫu.

Không tăng queue để “sửa” overload realtime; queue lớn thường chỉ tăng độ trễ và RAM.

## Benchmark

`omnidetect_benchmark` hiện đo đường portable bằng backend no-op ở input 320/416/640; số đó không phải tốc độ NCNN/ONNX. Benchmark thực tế phải dùng đúng binary Release, model, backend, nhiệt độ môi trường và camera count triển khai.

Ghi tối thiểu: commit, OS/kernel, model hash, input size, backend/options, camera/source FPS, thời lượng warm-up, median/P95 latency, per-camera inference FPS, CPU%, RSS, nhiệt độ và throttling.

## Soak 8–24 giờ

Script [multi_camera_soak.py](../tools/stress/multi_camera_soak.py) chạy ứng dụng thật, lấy CPU/RSS/nhiệt độ từ `/proc` và sysfs, lưu CSV/JSON, đếm log disconnect/recover và gửi SIGTERM khi hết thời gian.

```bash
python3 tools/stress/multi_camera_soak.py \
  --duration-hours 8 --interval-seconds 10 \
  --output-dir soak-8h -- \
  ./build-pi/omnidetect_edge --config /etc/omnidetect/config.yaml \
    --metrics-interval-seconds 10
```

Chạy 24 giờ bằng `--duration-hours 24`. Script không tự tuyên bố pass; người vận hành phải đặt ngưỡng theo deployment. Gợi ý kiểm tra:

- RSS không tăng theo xu hướng dài hạn sau warm-up;
- không có crash/deadlock và shutdown nằm trong `TimeoutStopSec`;
- nhiệt độ/throttling trong giới hạn thiết bị;
- inference FPS từng camera không rơi về 0 ngoài khoảng reconnect;
- tỷ lệ stale/drop phù hợp SLA;
- recovery hoạt động sau khi ngắt/mở lại một stream.

Kho mã không kèm kết quả soak giả lập thay cho thử nghiệm phần cứng.
