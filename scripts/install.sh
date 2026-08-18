#!/bin/bash
# On-device install for the e-CAM200 driver on JetPack 7 / L4T R39.
# Run from the repo root with sudo, AFTER building the module (cd driver && make).
# Assumes the Orin Nano devkit (p3767-0005). Adjust BASE_DTB for other SKUs.
set -eu

KREL=$(uname -r)
BASE_DTB=/boot/tegra234-p3768-0000+p3767-0005-nv.dtb
LANES=${LANES:-2lane}          # 2lane (either connector) or 4lane (CAM1 only)

[ -f driver/e-con_cam.ko ] || { echo "build first: cd driver && make"; exit 1; }
[ -f "$BASE_DTB" ] || { echo "base DTB not found: $BASE_DTB"; exit 1; }

# 1. kernel module (+ autoload)
mkdir -p /lib/modules/$KREL/extra
cp driver/e-con_cam.ko /lib/modules/$KREL/extra/
depmod -a
echo e-con_cam > /etc/modules-load.d/ecam200.conf

# 2. sensor firmware — NOT redistributed here. Copy ar2020_cam_fw.bin from
#    e-con's release package into /lib/firmware (and the kernel-cmdline
#    firmware path if one is set, e.g. /etc/firmware).
if [ ! -f /lib/firmware/ar2020_cam_fw.bin ]; then
  echo "WARNING: /lib/firmware/ar2020_cam_fw.bin missing — copy it from the"
  echo "         e-con release package or the driver will not probe."
fi

# 3. device tree: merge overlay into the base DTB (validated offline),
#    then boot it via a dedicated extlinux entry. The stock 'primary'
#    entry is left untouched as a recovery fallback.
fdtoverlay -i "$BASE_DTB" -o /boot/ecam200-merged-$LANES.dtb devicetree/ecam200-$LANES.dtbo
# vendor DT fixes required on R39 (see docs/BRINGUP.md):
for cam in /bus@0/cam_i2cmux/i2c@0/ar2020_a@42 /bus@0/cam_i2cmux/i2c@1/ar2020_c@42; do
  fdtput -t s /boot/ecam200-merged-$LANES.dtb $cam use_sensor_mode_id false 2>/dev/null || true
  for m in mode0 mode1 mode2 mode3; do
    fdtput -t s /boot/ecam200-merged-$LANES.dtb $cam/$m default_exp_time 8000 2>/dev/null || true
  done
done

if ! grep -q "LABEL ecam" /boot/extlinux/extlinux.conf; then
  cp /boot/extlinux/extlinux.conf /boot/extlinux/extlinux.conf.backup-preecam
  APPEND=$(sed -n '/LABEL primary/,/APPEND/{/APPEND/p}' /boot/extlinux/extlinux.conf | head -1)
  cat >> /boot/extlinux/extlinux.conf <<EOF

LABEL ecam
      MENU LABEL primary kernel + e-CAM200 camera
      LINUX /boot/Image
      INITRD /boot/initrd
      FDT /boot/ecam200-merged-$LANES.dtb
$APPEND
EOF
  sed -i 's/^DEFAULT primary/DEFAULT ecam/' /boot/extlinux/extlinux.conf
else
  sed -i "s|FDT /boot/ecam200-merged-.*.dtb|FDT /boot/ecam200-merged-$LANES.dtb|" /boot/extlinux/extlinux.conf
fi

echo "installed — reboot to bring the camera up ($LANES)"
