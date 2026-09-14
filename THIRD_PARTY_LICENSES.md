# Third-party licensing notes

OmniDetectCore source code is provided under Apache License 2.0. Optional dependencies are not vendored and retain their own licenses. Verify the exact versions and transitive notices in every distributed binary.

| Dependency | Typical license | Used for |
|---|---|---|
| OpenCV | Apache-2.0 | Image/video/webcam and recording adapters |
| Qt 6 | LGPL-3.0/GPL/commercial, depending on edition/modules | Desktop UI |
| NCNN | BSD-3-Clause | Edge inference |
| ONNX Runtime | MIT | Desktop/reference inference |
| spdlog | MIT | Structured logging when installed |
| GoogleTest | BSD-3-Clause | Unit-test runner when installed |
| Ultralytics | AGPL-3.0 or enterprise terms depending on use/version | Offline model export only |

Model weights and training datasets may carry separate restrictions. Their provenance and license must be recorded with each released artifact.

