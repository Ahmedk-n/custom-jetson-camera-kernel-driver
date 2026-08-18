# Custom Kernel Camera Driver — 20 MP MIPI-CSI on JetPack 7

**Bringing an unsupported camera to an unsupported OS: a Linux kernel driver
port, two driver bug fixes, and full device-tree bring-up for the e-con
e-CAM200 (onsemi AR2020, 20 MP) on the NVIDIA Jetson Orin Nano running
JetPack 7 / L4T R39 — a platform combination with no vendor support.**

This driver flies: it is the mission camera of the **ASCENT** autonomous
search UAS (KFUPM, SUAS 2026), feeding 2560×1920 raw Bayer into a tiled
YOLO TensorRT pipeline on the same Orin Nano.

## Why this exists

The vendor supports this camera only up to JetPack 6.2 (kernel 5.15). Our
airframe runs JetPack 7 (kernel 6.8) and can't go back. So we took the
vendor's JetPack 6 reference driver and made the platform leap ourselves:

1. **Kernel API port (5.15 → 6.8)** — GPIO descriptor migration, the
   one-argument i2c probe, void remove; out-of-tree build against JetPack
   7's `nvidia-public` headers with a stub for NVIDIA's generated
   `conftest.h`.
2. **Device-tree bring-up** — the vendor's overlays only exist inside the
   JetPack 6 build tree. We compile them standalone on-device, validate
   with `fdtoverlay` against NVIDIA's base DTB, and boot a pre-merged blob
   through a dedicated extlinux entry with the stock entry kept as a
   recovery fallback. No reflash, no risk of a bricked board.
3. **Two real driver bugs found and fixed during bring-up** (they exist on
   JetPack 6 too, but that platform's userspace happens to sidestep them):
   - a **stale-error latch**: one failed stream configuration left its
     error code latched in the camera MCU, and the driver's pre-check
     treated it as "ISP busy" forever after — a single bad request
     permanently bricked streaming until power cycle;
   - an **out-of-bounds mode index**: the driver registers the camera's
     *total* stream count but only fills its mode table with the Bayer
     streams, so the new tegracam framework selected a hole in the table
     and the driver asked the camera for a 0×0-pixel stream — which the
     camera's firmware rightly refused, tripping the latch above.

The diagnosis ran the full depth of the stack: CSI D-PHY interrupt traces,
VI capture-engine fault decoding (`CHANSEL` line-geometry faults), I²C
protocol instrumentation of the camera's MCU, and a byte-level look at the
raw frames (10-bit Bayer on a 2560-count black pedestal in a 16-bit
container). The bring-up log in [`docs/BRINGUP.md`](docs/BRINGUP.md) tells
the whole story, dead ends included.

## Repository layout

| Path | Contents |
|---|---|
| `driver/` | The kernel module source (GPL-2.0, based on e-con's reference driver) + out-of-tree Makefile |
| `devicetree/` | Overlay sources and compiled `.dtbo` for 2-lane and 4-lane CSI |
| `scripts/install.sh` | On-device build + install (module, firmware path, DTB merge, extlinux entry) |
| `docs/BRINGUP.md` | The full bring-up and debugging record |

## Quick start (on the Jetson)

```sh
cd driver && make                       # builds e-con_cam.ko against the running kernel
sudo ../scripts/install.sh              # installs module + DTB, adds boot entry
sudo reboot
v4l2-ctl -d /dev/video0 --list-formats-ext
```

The sensor's firmware blob (`ar2020_cam_fw.bin`) ships in e-con's release
package and is not redistributed here — `install.sh` tells you where to put
it.

## Status

| | |
|---|---|
| Module build/load on 6.8.12-tegra | ✅ |
| Sensor probe, firmware verify, controls | ✅ |
| Streaming (2560×1920, 10-bit Bayer GRBG) | ✅ |
| Full pipeline (debayer → YOLO TensorRT on-device) | ✅ ~2.3 inferences/s |
| Full frame rate (30–60 fps) | ⏳ camera firmware caps raw taps at 2.5 fps; needs the vendor's Argus path, which awaits their JetPack 7 release |

## License & credit

GPL-2.0. The driver is a port of e-con Systems' JetPack 6 reference driver
(copyright headers preserved); the kernel 6.8 port, both bug fixes, the
standalone device-tree bring-up, and the diagnosis are original work by the
ASCENT team.
