# GK7201V200 — vendor material and working files

This branch is **not code and is not meant to be merged**. It is the archive behind the
`gk7201v200-support` series: the vendor firmware the port was derived from, plus the working
files that were deliberately kept out of the pull request.

It exists because one file in it can never be produced again. The camera it came from is
assembled, and an assembled unit of this model has no serial console and no u-boot access, so
its flash cannot be read a second time.

## vendor/

| file | what it is |
|------|-----------|
| `xm720_full_16mb.bin` | **the irreplaceable one.** Full 16 MiB NOR dump of the camera's factory firmware, read with `defib` on 2026-07-13. md5 `d87d463bcc7ed9a6f9f11be2814ee914`. Everything this port knows about the vendor side traces back to this file. |
| `vendor_gk7201v200/`, `vendor_gk7201v200_stack.tar.gz` | unpacked pieces of that image |
| `dts/`, `extract/` | the device tree and the extraction scratch space, including the defib install log |
| `vendor_xm_modules.tgz`, `vendor_sinit.tgz` | the vendor MPP modules and `sinit.ko` as first pulled out of the image. Superseded by `files/kmod.gk7201v200/` on the support branch, which carries the same files with a `PROVENANCE.md` |
| `bits/` | individual pieces worth keeping: the vendor's own `load_media.sh` and load script as reference, the SC2336P IQ blob, and **both variants of `xm_sysconfig.ko`** — the vendor one and the osdrv one, which differ and are easy to confuse |

The device tree inside the image is uncompressed and carries `model = "xmedia XM72010200 DEMO
Board"` / `compatible = "xmedia,xm72010200"`, which is what identifies the SoC beyond doubt —
`xm72010200` **is** GK7201V200. `tools/idfw.sh` reads that straight out of any dump.

## patches/

Two kernel patches used during development and deliberately **not** shipped in the PR:

- `0900-arm-oops-raw-addresses.patch` — resolving a module symbol inside a vendor `xm_*.ko`
  faults (NULL `strtab`), and because that lookup happens inside the Oops printer, one fault
  becomes a recursive fault storm that wedges the board instead of printing a trace. Printing raw
  addresses breaks the recursion. Left out because it degrades Oops output for every ARM board in
  the tree, and because the hardware watchdog now resets a wedged board anyway. **Apply this
  locally before debugging any vendor-module Oops on this SoC.**
- `0901-arm-kprobes-reset-current-kprobe-when-pre-handler-returns-nonzero.patch` — on ARM,
  `reset_current_kprobe()` is only called when a pre-handler returns 0, so any handler using the
  "modify `regs->ARM_pc` and return non-zero" idiom leaves `current_kprobe` set forever and every
  later kprobe in the system silently stops calling its handler. Left out because every handler
  the port ships returns 0. Needed again the moment a redirecting handler is added.

## tools/

Host- and device-side scripts written during the port:

- `idfw.sh` — identify the SoC, sensor and radio of a XiongMai/Goke camera from a raw flash dump,
  without unpacking anything. Run on a host.
- `subtest.sh` — the substream/rotation test harness: arms a case, reboots, and measures the
  first majestic of that boot with `/proc/umap/vpss` sampled twice so counters are seen climbing.
- `regress.sh` — full-feature regression on a flashed image.
- `postflash.sh` — the focused checks for the final image, including the watchdog-release proof.

## doc/

- `PR-gk7201v200.md` — the pull request text.
- `attic/` — superseded working files: the original hand-built `libgk_shim.so`, a `.mk` backup,
  the stray board config from the abandoned first approach, and `kmod.osdrv/` (an osdrv variant
  module set whose `xm_sysconfig.ko` differs from both the stock and the vendor one).

## The port itself

Branch `gk7201v200-support` — nine commits against OpenIPC master. Read
`general/package/goke-osdrv-gk7205v500/files/kmod.gk7201v200/PROVENANCE.md` there for how the
vendor modules were extracted and how to repeat it.
