#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
PROBE_DIR = ROOT / "app/src/main/kotlin/com/vruacom/s26scannerlab/probe"

camera_files = [
    "CameraProbe.kt",
    "LaserAfProbe.kt",
    "RawProbe.kt",
    "DepthProbe.kt",
    "FlashCaptureProbe.kt",
    "BasicScanRecorder.kt",
    "ConcurrentCameraProbe.kt",
]

errors = []
for name in camera_files:
    path = PROBE_DIR / name
    if not path.exists():
        errors.append(f"missing: {path}")
        continue
    text = path.read_text(encoding="utf-8")
    if "shutdownNow()" in text or ".shutdown()" in text:
        errors.append(f"{name}: must not shut down Camera2 callback executor")
    if "CameraCallbackExecutor.executor" not in text:
        errors.append(f"{name}: must use CameraCallbackExecutor.executor")

shared = PROBE_DIR / "CameraCallbackExecutor.kt"
if not shared.exists():
    errors.append("missing CameraCallbackExecutor.kt")
else:
    text = shared.read_text(encoding="utf-8")
    if "newCachedThreadPool" not in text:
        errors.append("CameraCallbackExecutor.kt: expected process-wide cached executor")

if errors:
    print("camera executor lifecycle verification FAILED")
    for error in errors:
        print(f" - {error}")
    sys.exit(1)

print("camera executor lifecycle verification PASS")
