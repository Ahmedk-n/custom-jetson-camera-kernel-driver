# e-CAM200_CUONX on JetPack 7 / L4T R39.2 — driver port + bring-up log

e-con Systems supports this 20MP AR2020 MIPI camera only up to JetPack 6.2
(L4T 36.4.3). Our Jetson Orin Nano runs JetPack 7 (L4T R39.2, kernel
6.8.12-1021-tegra). This directory holds the working port of e-con's
L4T 36.5.0 R02_RC3 release driver to R39, plus the compiled device-tree
overlays and the full field-debug record.

## What was ported (2026-08-17)

**Driver** (`driver/`) — extracted from e-con's
`e-CAM200_CUONX_JETSON-ONX-ONANO_L4T36.5.0_module.patch`, three kernel
5.15 → 6.8 API fixes applied:

1. `gpio_cansleep(gpio)` → `gpiod_cansleep(gpio_to_desc(gpio))`
   (+ `#include <linux/gpio/consumer.h>`)
2. two-arg i2c probe → one-arg, id recovered via
   `i2c_client_get_device_id(client)`
3. `int cam_remove(...) { ... return 0; }` → `void cam_remove(...)`

`conftest-shim.h` stands in for NVIDIA's generated `nvidia/conftest.h`
(only `NV_TEGRA_PMC_IO_PAD_POWER_ENABLE_PRESENT` is consulted).

Build on the Jetson:

```sh
make -C /lib/modules/$(uname -r)/build M=$PWD \
     KBUILD_EXTRA_SYMBOLS=/usr/src/nvidia/nvidia-public/Module.symvers modules
```

**Device tree** (`devicetree/`) — e-con's overlay sources compile with the
stock kernel-headers dt-bindings after substituting
`JETSON_COMPATIBLE_P3768` with the board's real compatible
(`nvidia,p3768-0000+p3767-0005`). Both `.dtbo` files apply cleanly to the
NVIDIA base DTB with `fdtoverlay`. We boot a **pre-merged** DTB via an
explicit `FDT` line in a second extlinux entry (fallback `primary` entry
kept untouched):

```
LABEL ecam
      FDT /boot/ecam200-merged-4lane.dtb     # or -2lane
```

- `2lane`: camera on either CAM0 or CAM1, 2 CSI lanes (1.5 Gbps/lane)
- `4lane`: camera on CAM1 only, 4 CSI lanes (750 Mbps/lane) — e-con default

**Installed on the Jetson:**
- `/lib/modules/$(uname -r)/extra/e-con_cam.ko` + `/etc/modules-load.d/ecam200.conf`
- firmware `ar2020_cam_fw.bin` → `/etc/firmware/` (kernel cmdline path) and `/lib/firmware/`
- ISP tuning `camera_overrides_jetson-onano.isp` → `/var/nvidia/nvcam/settings/camera_overrides.isp`
- extlinux backup: `/boot/extlinux/extlinux.conf.backup-preecam`

## Port result: SUCCESS at the software level

- module loads, links against `tegra_camera`
- sensor probes: `/dev/video0`, full mode list
  (5120×3840@15 / 3840×2160@25 / 2560×1920@30 / 1920×1080@60, 10-bit Bayer GRBG)
- MCU firmware verified (`1_2_E-CAM200XXX0_4_1_1_0_305d1de`), CAM INIT succeeds
- **on the first-ever boot the sensor streamed** (VI received frames)

## RESOLVED 2026-08-17: camera fully working

Final state: camera on **CAM0**, booted with the **2-lane merged DTB**
(`FDT /boot/ecam200-merged-2lane.dtb`), plus two DT property fixes applied
with fdtput to the merged blob:
- `use_sensor_mode_id = "false"` (both sensor nodes) — R39's tegracam trusts
  a bogus default sensor-mode control otherwise
- `embedded_metadata_height` left at `"0"` (stock)

Working capture: **2560x1920 @ 2.5 fps, 10-bit Bayer GRBG in 16-bit
container**, via raw V4L2 (`v4l2-ctl --stream-to=-`). The detector's `csi`
backend debayers in software. Full chain verified: sensor → VI → debayer →
YOLO v9 TensorRT tiled ≈ 2.3 det/s. `ascent-stream` now serves the overlay
from this camera.

Known limitations (open):
- Only the 2560x1920 mode aligns between what R39 negotiates and what the
  sensor streams; requests for other sizes get mismapped (PIXEL_LONG/SHORT
  line faults). 2560x1920 is full-FOV 4:3 and fine for detection.
- Frame rate is pinned at 2.5 fps — the camera firmware's RAW-mode floor
  (e-con's app manual: "frame rate range … minimum value of 2.5"). Ruled
  out: exposure length (brightness responds to the ctrl, frame period does
  not), the `frame_rate` ctrl (MCU ACKs 30 fps yet still emits exact 400 ms
  frames, consecutive sequence numbers = no drops), the stream_config rate
  field (sends @30), DT `default_exp_time`, `jetson_clocks`, `nvpmodel`.
  Conclusion: the MCU honors its full rates only through e-con's Argus
  path; a raw V4L2 tap runs at the floor. Ticket question for e-con: which
  MCU command selects RAW-mode frame timing? Missions are unaffected —
  2.5 fps matches the tiled detector's ~2.3 det/s throughput.
- Argus/nvargus cannot open the sensor on JP7 (`V4L2SensorViCsi initialize`
  fails) — software debayer instead; the ISP tuning file is installed for
  whenever e-con ships JP7 support.

## The debugging record (kept for e-con / posterity)

Chronology:

1. First boot (camera in CAM0, original seating): sensor transmits, but VI
   discards every frame — `corr_err ... err_data 512`, RTCPU trace shows
   D-PHY start-of-transmission multi-bit errors. 2-lane mode runs at
   exactly 1.5 Gbps/lane (the deskew threshold) and **R39 ships without
   the CSI deskew driver** (`csi/deskew.c` is not compiled into
   `tegra-camera.ko` — verified from the Ubuntu dkms source package).
2. After any re-plug (CAM0 reseat, then move to CAM1, cold boots): data
   lanes totally silent; I2C control path always perfect.
3. Root symptom isolated: the camera's **onboard ISP no longer boots**.
   Probe-time `CAM INIT` reports success, but at stream time the MCU
   reports `ISP Uninitialized STATUS=0x0000 Errcode=0x0a` →
   `Error writing mode` → no MIPI output. Reproduced across both
   connectors, both lane configs, warm and cold boots.

Working theory: marginal power delivery to the camera module (ISP boot is
the peak-current moment; the low-power MCU survives, the ISP browns out) —
either a worn/damaged FFC contact or module damage from live re-plugging.
Note the connectors were re-seated while the system was powered at least
once (CSI is not hot-plug safe).

Next steps: multi-minute full power drain, FFC inspection/replacement,
e-con support ticket (this file is the evidence chain), possible RMA.

## Software integration (ready and waiting)

`mannequin-perception/camera_detector.py` already has the `csi` backend
(`--camera csi`): nvarguscamerasrc first, `v4l2src /dev/video0` fallback.
Once the camera streams, the end-to-end check is:

```sh
~/ASCENT/.venv/bin/python preview_detector.py --camera csi --tile auto
```
