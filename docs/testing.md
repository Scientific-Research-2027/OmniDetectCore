# Chiến lược kiểm thử

## Tự động

```bash
cmake --preset portable-release
cmake --build --preset portable-release
ctest --preset portable-release
```

Nếu không có GoogleTest, executable dùng runner tương thích không dependency. Test hiện bao phủ:

- quyền sở hữu/copy/move của `Frame`;
- letterbox, map tọa độ, decode/filter/NMS YOLO;
- queue giới hạn và EventEngine snapshot;
- tracker/monitor temporal;
- pipeline single-camera start/stop/restart;
- 4 camera chia sẻ một detector, không gọi detector đồng thời và có fairness;
- routing đúng `cameraId`, reconnect độc lập, loại stale frame;
- monitor nhiều track/camera không lẫn state;
- parse/validate YAML nhiều camera và ID/FPS sai;
- control queue, manual override, Auto‑PTZ target/dead-zone/lost timeout bằng mock.

Test mock không chứng minh camera/backend phần cứng hoạt động.

## Ma trận thủ công trước phát hành

1. ONNX Windows/Linux với ảnh chuẩn và output shape đã biết.
2. NCNN ARM64 CPU với model hash phát hành.
3. 2 và 4 RTSP stream, gồm mất/kết nối lại từng stream.
4. CSI/libcamera nếu build hỗ trợ.
5. Một camera có FPS cao hơn để xác nhận fairness/priority.
6. Sink chậm, ổ đầy/quota recorder và đường output không ghi được.
7. SIGINT/SIGTERM khi capture, inference, reconnect và PTZ đang hoạt động.
8. ONVIF/V4L2 thật ở tốc độ thấp, kiểm tra chiều trục và STOP.
9. Soak 8 giờ trước, sau đó 24 giờ trên Pi có tải/nguồn/mạng thật.

Mọi kết quả hardware phải ghi rõ thiết bị và điều kiện; không thay bằng log mock.
