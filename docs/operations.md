# Vận hành trên Raspberry Pi 5

## Chuẩn bị

1. Dùng Raspberry Pi OS 64-bit và bật camera interface phù hợp.
2. Xác nhận từng nguồn độc lập bằng `ffprobe`/VLC cho RTSP hoặc `libcamera-hello`/pipeline GStreamer cho CSI.
3. Build OpenCV có FFmpeg; nếu dùng libcamera adapter, xác nhận `cv::getBuildInformation()` có GStreamer.
4. Export đúng YOLO26n, lưu `.param` + `.bin` cho NCNN và kiểm tra class order.
5. Chạy unit test trước khi nối camera thật.

CPU-only là baseline an toàn. Chỉ bật Vulkan sau khi xác nhận image, driver và NCNN build tương thích; không suy ra lợi ích nếu chưa đo.

## Cài đặt systemd

Sao chép binary/library vào `/opt/omnidetect`, config vào `/etc/omnidetect`, và dùng [omnidetect.service](../deploy/systemd/omnidetect.service) làm mẫu. Tạo user riêng, thư mục ghi dữ liệu và file environment:

```bash
sudo useradd --system --home /var/lib/omnidetect --shell /usr/sbin/nologin omnidetect
sudo install -d -o omnidetect -g omnidetect /var/lib/omnidetect /var/log/omnidetect
sudo install -m 600 -o root -g root camera.env /etc/omnidetect/camera.env
sudo systemctl daemon-reload
sudo systemctl enable --now omnidetect
journalctl -u omnidetect -f
```

Nếu dùng biến môi trường, thêm `EnvironmentFile=/etc/omnidetect/camera.env` vào unit triển khai. Không ghi secret ra journal.

## Kiểm tra sức khỏe

Theo dõi theo camera: state, tuổi frame/inference cuối, capture/inference FPS, drop/skip/stale, reconnect và lỗi inference. Theo dõi toàn cục: detector ready, inference utilization, P95 latency, router drop và nhiệt độ Pi.

Dấu hiệu cần giảm tải:

- `last_inference_age` tăng liên tục;
- `framesStale` hoặc `router.dropped` tăng nhanh;
- P95 tăng cùng nhiệt độ/throttling;
- camera priority thấp gần như không có inference;
- reconnect tăng đồng thời ở nhiều camera (mạng/nguồn chung).

Giảm lần lượt: `inference_fps`, độ phân giải capture, `max_total_fps`, input size model; sau đó mới cân nhắc tăng thread hoặc Vulkan.

## Sự cố thường gặp

- RTSP không mở: kiểm tra URI ngoài ứng dụng, FFmpeg trong OpenCV, firewall và quyền tài khoản. Log cố ý không in URI.
- libcamera không mở: kiểm tra GStreamer plugin `libcamerasrc`, camera-name và quyền thiết bị.
- ONVIF không chạy: kiểm tra build flag/libcurl, PTZ endpoint, profile token, quyền user và HTTPS certificate.
- V4L2 không PTZ: dùng `v4l2-ctl --list-ctrls -d /dev/videoN`; không phải driver nào cũng có pan/tilt/zoom.
- shutdown chậm: đo timeout đọc của transport/driver và giảm ở adapter; không kill cứng trước khi recorder đóng file trừ tình huống khẩn cấp.
