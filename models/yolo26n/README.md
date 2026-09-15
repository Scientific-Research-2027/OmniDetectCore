# Model YOLO26n

Thư mục này chỉ chứa artifact model cục bộ; weight bị loại khỏi Git theo `.gitignore`.

- ONNX: đặt file ở `model.onnx` hoặc sửa `model.path`.
- NCNN: đặt `.param` ở `model.path` và `.bin` ở `model.weights_path`.
- Lưu `metadata.json` và `classes.txt` từ công cụ export để kiểm tra input/output, kích thước ảnh và thứ tự lớp.

Không giả định mọi bản export YOLO26 có cùng shape. Hãy chạy test ảnh chuẩn và kiểm tra log shape trước khi triển khai.
