#!/usr/bin/env python3
"""Export a training checkpoint for OmniDetectCore; never used by the C++ runtime."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description="Export YOLO26 weights to ONNX or NCNN")
    parser.add_argument("checkpoint", type=Path, help="PyTorch/Ultralytics .pt checkpoint")
    parser.add_argument("--format", choices=("onnx", "ncnn"), required=True)
    parser.add_argument("--imgsz", type=int, default=416)
    parser.add_argument("--output", type=Path, default=Path("models/yolo26n"))
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--opset", type=int, default=17)
    parser.add_argument("--half", action="store_true", help="Request FP16 export when supported")
    arguments = parser.parse_args()

    if not arguments.checkpoint.is_file():
        parser.error(f"checkpoint does not exist: {arguments.checkpoint}")
    if arguments.imgsz <= 0 or arguments.imgsz % 32 != 0:
        parser.error("--imgsz must be a positive multiple of 32")
    arguments.output.mkdir(parents=True, exist_ok=True)

    try:
        from ultralytics import YOLO
    except ImportError as exception:
        raise SystemExit("Install tools/model_export/requirements.txt before exporting") from exception

    model = YOLO(str(arguments.checkpoint))
    export_options = {
        "format": arguments.format,
        "imgsz": arguments.imgsz,
        "device": arguments.device,
        "half": arguments.half,
    }
    if arguments.format == "onnx":
        export_options.update({"opset": arguments.opset, "simplify": True, "dynamic": False})
    exported = Path(model.export(**export_options))

    destination = arguments.output / exported.name
    if exported.resolve() != destination.resolve():
        if exported.is_dir():
            if destination.exists():
                shutil.rmtree(destination)
            shutil.copytree(exported, destination)
        else:
            shutil.copy2(exported, destination)

    names = model.names
    ordered_names = [str(names[index]) for index in sorted(names)] if isinstance(names, dict) else list(names)
    (arguments.output / "classes.txt").write_text("\n".join(ordered_names) + "\n", encoding="utf-8")

    artifacts = [path for path in arguments.output.rglob("*") if path.is_file() and path.name != "metadata.json"]
    metadata = {
        "detector": "yolo26",
        "format": arguments.format,
        "input_size": [arguments.imgsz, arguments.imgsz],
        "input_layout": "NCHW",
        "input_color": "RGB",
        "normalization_scale": 1.0 / 255.0,
        "class_names": ordered_names,
        "export_options": export_options,
        "artifacts": {path.relative_to(arguments.output).as_posix(): sha256(path) for path in artifacts},
        "note": "Inspect and record actual input/output blob names and output layout for this exporter version.",
    }
    (arguments.output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(f"Exported {arguments.format} artifacts to {arguments.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

