#!/usr/bin/env python3
import sys
import zipfile
from pathlib import Path

if len(sys.argv) < 3:
    print("Usage: package-twrp-zip.py <boot.img> <output.zip>")
    sys.exit(1)

boot_img_path = Path(sys.argv[1])
output_zip_path = Path(sys.argv[2])

if not boot_img_path.is_file():
    print(f"Error: {boot_img_path} not found")
    sys.exit(1)

update_binary = Path(__file__).with_name("flash-boot-recovery.sh").read_text(encoding="utf-8")

output_zip_path.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(output_zip_path, "w", zipfile.ZIP_DEFLATED) as z:
    updater = zipfile.ZipInfo("META-INF/com/google/android/update-binary")
    updater.create_system = 3
    updater.external_attr = (0o100755 << 16)
    z.writestr(updater, update_binary.encode("utf-8"), compress_type=zipfile.ZIP_DEFLATED)
    z.writestr("META-INF/com/google/android/updater-script", "")
    z.write(boot_img_path, arcname="boot.img")

print(f"Successfully packaged TWRP flashable zip: {output_zip_path} ({output_zip_path.stat().st_size} bytes)")
