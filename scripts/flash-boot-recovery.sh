#!/sbin/sh
# Recovery updater for the beyond2lte XNU BOOT image.
# TWRP invokes this as: update-binary <api> <outfd> <zipfile>
set -eu

OUTFD="${2:-}"
ZIP_PATH="${3:-}"
WORK="/tmp/b2l-boot-flash"
BACKUP_DIR="/sdcard/b2l-boot-backups"

ui_print() {
  if [ -n "$OUTFD" ] && [ -e "/proc/self/fd/$OUTFD" ]; then
    printf 'ui_print %s\nui_print\n' "$1" >> "/proc/self/fd/$OUTFD"
  else
    echo "$1"
  fi
}

fail() {
  ui_print "ERROR: $1"
  exit 1
}

ui_print "beyond2lte BOOT installer"
ui_print "No userdata partition will be modified."

[ -n "$ZIP_PATH" ] && [ -f "$ZIP_PATH" ] || fail "Recovery did not provide a valid ZIP path."
command -v unzip >/dev/null 2>&1 || fail "unzip is unavailable in recovery."
command -v dd >/dev/null 2>&1 || fail "dd is unavailable in recovery."
command -v sha256sum >/dev/null 2>&1 || fail "sha256sum is unavailable; refusing an unverifiable write."
command -v blockdev >/dev/null 2>&1 || fail "blockdev is unavailable; cannot verify partition capacity."

MODEL="$(getprop ro.product.model 2>/dev/null || true)"
DEVICE="$(getprop ro.product.device 2>/dev/null || true)"
BOOTLOADER="$(getprop ro.bootloader 2>/dev/null || true)"
case "$MODEL:$DEVICE:$BOOTLOADER" in
  SM-G975F:*:*|*:beyond2lte:G975F*)
    ;;
  *)
    fail "Device identity mismatch (model='$MODEL', device='$DEVICE', bootloader='$BOOTLOADER'); expected SM-G975F/beyond2lte."
    ;;
esac

rm -rf "$WORK"
mkdir -p "$WORK" "$BACKUP_DIR" || fail "Cannot create temporary/backup directories."
unzip -p "$ZIP_PATH" boot.img > "$WORK/boot.img" || fail "Could not extract boot.img."
[ -s "$WORK/boot.img" ] || fail "Extracted boot.img is empty."

# The artifact must be an Android boot image containing this project's trailer.
[ "$(dd if="$WORK/boot.img" bs=1 count=8 2>/dev/null)" = "ANDROID!" ] || fail "Image does not have Android boot-image magic."
grep -a -q 'B2LXNU02' "$WORK/boot.img" || fail "Image does not contain the expected B2LXNU02 XNU trailer."

# Resolve exactly one BOOT partition. Ambiguous or missing paths are fatal.
CANDIDATES=""
for p in /dev/block/platform/*/by-name/boot; do
  [ -b "$p" ] || continue
  CANDIDATES="$CANDIDATES
$p"
done
# /dev/block/by-name is commonly a symlink to the platform path; use it only
# as a fallback so the same partition is not counted twice.
if [ -z "$CANDIDATES" ] && [ -b /dev/block/by-name/boot ]; then
  CANDIDATES="/dev/block/by-name/boot"
fi
COUNT="$(printf '%s\n' "$CANDIDATES" | sed '/^$/d' | wc -l | tr -d ' ')"
[ "$COUNT" = "1" ] || fail "Expected exactly one block-device by-name/boot; found $COUNT."
BOOT_DEV="$(printf '%s\n' "$CANDIDATES" | sed '/^$/d' | head -n 1)"
[ -b "$BOOT_DEV" ] || fail "Resolved BOOT target is not a block device."

IMAGE_BYTES="$(wc -c < "$WORK/boot.img" | tr -d ' ')"
PART_BYTES="$(blockdev --getsize64 "$BOOT_DEV" 2>/dev/null || true)"
case "$PART_BYTES" in
  ''|*[!0-9]*) fail "Could not read BOOT partition capacity." ;;
esac
[ "$PART_BYTES" -ge "$IMAGE_BYTES" ] || fail "Image ($IMAGE_BYTES bytes) exceeds BOOT partition ($PART_BYTES bytes)."

STAMP="$(date +%Y%m%d-%H%M%S 2>/dev/null || echo recovery)"
BACKUP="$BACKUP_DIR/boot-before-b2l-$STAMP.img"
ui_print "Target: $BOOT_DEV"
ui_print "Image: $IMAGE_BYTES bytes; partition: $PART_BYTES bytes"
ui_print "Creating backup: $BACKUP"

# Ensure there is enough free space before reading the existing partition.
AVAIL_KB="$(df -k "$BACKUP_DIR" 2>/dev/null | tail -n 1 | awk '{print $4}')"
NEEDED_KB=$(( (PART_BYTES + 1023) / 1024 ))
case "$AVAIL_KB" in
  ''|*[!0-9]*) fail "Cannot verify backup free space." ;;
esac
[ "$AVAIL_KB" -ge "$NEEDED_KB" ] || fail "Not enough free space for a full BOOT backup."

dd if="$BOOT_DEV" of="$BACKUP" bs=4M 2>/dev/null || fail "BOOT backup read failed."
sync
BACKUP_BYTES="$(wc -c < "$BACKUP" | tr -d ' ')"
[ "$BACKUP_BYTES" -ge "$PART_BYTES" ] || fail "BOOT backup is shorter than the partition; refusing to write."
BACKUP_HASH="$(sha256sum "$BACKUP" | awk '{print $1}')"
[ -n "$BACKUP_HASH" ] || fail "Could not verify backup checksum."

IMAGE_HASH="$(sha256sum "$WORK/boot.img" | awk '{print $1}')"
ui_print "Backup verified. Writing BOOT..."
dd if="$WORK/boot.img" of="$BOOT_DEV" bs=4M conv=fsync 2>/dev/null || fail "BOOT write failed. Keep the backup; do not reboot until recovery is assessed."
sync

dd if="$BOOT_DEV" of="$WORK/readback.img" bs=4M count="$(( (IMAGE_BYTES + 4194303) / 4194304 ))" 2>/dev/null || fail "Could not read back BOOT for verification."
READBACK_HASH="$(head -c "$IMAGE_BYTES" "$WORK/readback.img" | sha256sum | awk '{print $1}')"
[ "$READBACK_HASH" = "$IMAGE_HASH" ] || fail "Read-back checksum mismatch. Backup is at $BACKUP."

ui_print "BOOT write and read-back checksum verified."
ui_print "Backup retained at: $BACKUP"
ui_print "userdata was not touched."
rm -rf "$WORK"
exit 0
