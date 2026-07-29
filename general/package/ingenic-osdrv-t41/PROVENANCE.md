# Provenance of the files in this package

Everything here is byte-identical to a public upstream source. Nothing was
extracted from a camera image, and nothing has been modified in the repository.
The one transformation that is applied — dropping the uClibc `NEEDED` entries so
the libraries load under musl — happens at build time in
`ingenic-osdrv-t41.mk`, in plain sight, and is described there.

Verify with `md5sum` against the sources below.

## Userspace MPP libraries — `files/lib/`

From <https://github.com/gtxaspec/ingenic-lib>, path
`T41/lib/1.2.6/uclibc/7.2.0/`.

| file | md5 |
|---|---|
| `libimp.so` | `db7c16e02f87d0f5a088c24ce0739171` |
| `libsysutils.so` | `fa9bc0a12889c06cd320f063212f9aa4` |
| `libalog.so` | `f564df3324a8d2373f5e41f532f63274` |

That release was chosen because it is the one running on the camera this port
was developed against: its `libimp.so` and `libsysutils.so` are byte-identical
to the working firmware's. (`libalog.so` there differs — that vendor appears to
rebuild it — so the matching 1.2.6 build is used for a coherent set.)

`libaudioProcess.so` from the same directory is deliberately **not** shipped. It
needs `libstdc++.so.6`, which is not in an OpenIPC lite image, so it could not
load; nothing currently in the image references it; and none of the other
Ingenic osdrv packages ship it either. If audio processing turns out to need
it, add it together with a C++ runtime, on evidence rather than in advance.

These are proprietary Ingenic binaries. They are the same class of file that
`ingenic-osdrv-t20/t21/t23/t30/t31/t40` already ship, and no open replacement
exists for T41: `open-tx-isp` is T31-only and `openimp` is unreleased.

### The uClibc problem

The libraries are uClibc builds and name `libc.so.0`, `libpthread.so.0` and
`libdl.so.0` in their `NEEDED` entries. musl provides none of those SONAMEs and
folds pthread and dl into libc, so the loader rejects them as shipped. The
entries are removed with `patchelf` during install, which lets the symbols
resolve against the musl already mapped into the process. OpenIPC's other
Ingenic osdrv packages ship libraries that have already had this done to them;
the difference here is that the files in the repository still match their
upstream checksums and the edit is visible in the build.

## ISP tuning data — `files/sensor/params/gc5603-t41.bin`

From <https://github.com/thingino/ingenic-sdk> (GPL-2.0+), path
`sensor-iq/t41/gc5603.bin`, md5 `85fd92a989a57fe031860dc7bce29ac4`.

Byte-identical to `/usr/share/sensor/gc5603-t41.bin` on the working camera.

## Sensor description — `files/sensor/gc5603.yaml`

Written for this package. The values are measured, not copied:

- `address: 0x31` — the sensor's i2c address on this board.
- `width/height: 2880x1620` — confirmed twice, from `.width`/`.height` in
  `openingenic`'s `kernel/sensors/t41/gc5603/gc5603.c` and from the working
  camera's own streamer configuration.

## Module loader — `files/script/load_ingenic`

Derived from `ingenic-osdrv-t40/files/script/load_ingenic` with three changes,
each commented at the point it occurs:

1. The U-Boot environment is read before `ipcinfo`, not after. `ipcinfo -f`
   succeeds while reporting the wrong family on some T41 parts, so an
   `ipcinfo -f || fw_printenv -n soc` fallback never fires.
   `ingenic-osdrv-t30` already reads the environment directly.
2. T41 module parameters: `avpu clk_name=sclka avpu_clk=550000000` and
   `tx-isp direct_mode=0`, which are the values the vendor firmware for this
   board runs with. The T40 defaults (`vpll`, 654 MHz, `isp_clk=350000000`)
   are for different silicon.
3. `fw_printenv -n sensor` failures are silenced so a missing variable falls
   through to sinfo autodetection rather than printing to the console.
