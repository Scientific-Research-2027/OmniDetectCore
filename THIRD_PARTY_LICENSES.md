# Ghi chú giấy phép bên thứ ba

Mã nguồn OmniDetectCore được cung cấp theo Apache License 2.0. Dependency tùy chọn không được vendoring và vẫn giữ giấy phép riêng. Khi phát hành binary, phải kiểm tra đúng phiên bản, dependency chuyển tiếp và notice tương ứng.

| Dependency | Giấy phép thường gặp | Mục đích |
|---|---|---|
| OpenCV | Apache-2.0 | Ảnh, video, webcam, RTSP, GStreamer và recorder |
| Qt 6 | LGPL-3.0/GPL/thương mại tùy edition/module | Giao diện desktop |
| NCNN | BSD-3-Clause | Inference edge |
| ONNX Runtime | MIT | Inference desktop/tham chiếu |
| libcurl | curl license | Lệnh PTZ ONVIF |
| spdlog | MIT | Logging khi được cài |
| GoogleTest | BSD-3-Clause | Test runner khi được cài |
| Ultralytics | Điều khoản tùy phiên bản/cách dùng | Export model ngoại tuyến |

Model weight và dataset có thể có hạn chế riêng. Phải ghi lại nguồn gốc và giấy phép cùng mỗi artifact phát hành.
