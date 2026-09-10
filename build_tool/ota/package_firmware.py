#!/usr/bin/env python3
"""依次生成 HTTP、Matter 升级包，并按项目命名导出烧录档和升级档。"""

import argparse
from datetime import datetime
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from pack_ota import read_define, write_file


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--factory", type=Path, required=True)
    parser.add_argument("--project-header", type=Path, required=True)
    parser.add_argument("--key-header", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--timestamp", required=True)
    parser.add_argument("--purpose", choices=["TEST", "FACTORY"], default="TEST")
    args = parser.parse_args()
    try:
        if not re.fullmatch(r"\d{8}\.\d{6}\.\d{3}", args.timestamp):
            raise ValueError("Timestamp must use YYYYMMDD.HHMMSS.mmm")
        datetime.strptime(args.timestamp, "%Y%m%d.%H%M%S.%f")
        fields = [read_define(args.project_header, name) for name in (
            "SONOFF_DEVICE_CLASS", "SONOFF_DEVICE_SERIAL_NUMBER", "SONOFF_DEVICE_FUNCTION",
            "SONOFF_DEVICE_CHIP", "SONOFF_SOFTWARE_VERSION_STRING")]
        if any(not re.fullmatch(r"[A-Za-z0-9_.-]+", value) for value in fields):
            raise ValueError("Firmware naming fields contain invalid characters")
        name = f"FW{'-'.join(fields)}-{args.timestamp}-{args.purpose}"
        vendor = int(read_define(args.project_header, "SONOFF_MATTER_VENDOR_ID"), 0)
        product = int(read_define(args.project_header, "SONOFF_MATTER_PRODUCT_ID"), 0)
        if not 0 < vendor <= 0xffff or not 0 < product <= 0xffff:
            raise ValueError("Matter VID and PID must be nonzero 16-bit IDs matching the device")
        version = int(read_define(args.project_header, "SONOFF_MATTER_SOFTWARE_VERSION"), 0)
        version_string = read_define(args.project_header, "SONOFF_MATTER_SOFTWARE_VERSION_STRING")
        for source in (args.input, args.factory):
            if not source.is_file():
                raise FileNotFoundError(f"Missing SDK output: {source}")

        scripts = Path(__file__).resolve().parent
        package_dir = args.input.parent
        http = package_dir / "ota_encrypted.bin"
        matter = package_dir / "ota_matter.ota"
        # 两层打包均成功后再更新产物和导出文件。
        with tempfile.TemporaryDirectory(prefix=".ota-", dir=package_dir) as directory:
            staged_http = Path(directory) / http.name
            staged_matter = Path(directory) / matter.name
            subprocess.run([
                sys.executable, str(scripts / "pack_ota.py"), "pack",
                "--input", str(args.input), "--output", str(staged_http),
                "--key-header", str(args.key_header), "--version", fields[-1],
            ], check=True)
            subprocess.run([
                sys.executable, str(scripts / "pack_matter_ota.py"), "create",
                "-v", str(vendor), "-p", str(product), "-vn", str(version),
                "-vs", version_string, "-da", "sha256", str(staged_http), str(staged_matter),
            ], check=True)
            staged_http.replace(http)
            staged_matter.replace(matter)

        for kind, source in (("flash", args.factory), ("http", http), ("matter", matter)):
            destination = args.out_dir / kind / (name + source.suffix)
            destination.parent.mkdir(parents=True, exist_ok=True)
            write_file(destination, source.read_bytes())
            print(f"Firmware exported: {destination}")
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Firmware packaging failed: {error}\n")


if __name__ == "__main__":
    main()
