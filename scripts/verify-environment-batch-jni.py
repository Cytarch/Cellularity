#!/usr/bin/env python3
import re
import subprocess
import sys
from pathlib import Path


if len(sys.argv) != 2:
    raise SystemExit("usage: verify-environment-batch-jni.py <libcellularity.so>")

root = Path(__file__).resolve().parents[1]
bridge_source = (
    root / "app/src/main/java/com/cytarch/cellularity/NativeCellularityControllerBridge.java"
).read_text(encoding="utf-8")
library = Path(sys.argv[1])

required_methods = {
    "nativeEnvironmentState",
    "nativeApplyEnvironmentPatch",
}
declarations = set(
    re.findall(r"private\s+static\s+native\s+byte\[\]\s+(native\w+)\s*\(", bridge_source)
)
missing_declarations = sorted(required_methods - declarations)
if missing_declarations:
    raise SystemExit(
        "missing Java environment batch declarations: " + ", ".join(missing_declarations)
    )

symbols = subprocess.check_output(["nm", "-D", str(library)], text=True)
bridge_exports = set(
    re.findall(
        r"Java_com_cytarch_cellularity_NativeCellularityControllerBridge_(native\w+)",
        symbols,
    )
)
missing_exports = sorted(required_methods - bridge_exports)
if missing_exports:
    raise SystemExit(
        "missing environment batch JNI exports: " + ", ".join(missing_exports)
    )

legacy_exports = sorted(
    set(re.findall(r"Java_com_cytarch_cellularity_NativeEnvironmentAccess_(native\w+)", symbols))
)
if legacy_exports:
    raise SystemExit(
        "legacy per-property environment JNI exports remain: "
        + ", ".join(legacy_exports)
    )

print("verified 2 environment batch JNI exports and no legacy property exports")
