# Provenance of the U-Boot patches

The board builds its own bootloader because OpenIPC has no working T41 one. The
2013-era Ingenic vendor U-Boot that OpenIPC's other Ingenic boards use
(`OpenIPC/u-boot-ingenic`) carries T10/T20/T21/T23/T30/T31 and has no XBurst2
T41 support at all, so there was nothing to extend.

The source is the **stock upstream U-Boot 2026.07 release tarball** from
<https://ftp.denx.de/pub/u-boot/u-boot-2026.07.tar.bz2>, fetched and hashed by
buildroot exactly as for any other package. Nothing is fetched from anywhere
else at build time. The three patches below are applied on top of it, in
filename order, and have been verified to apply cleanly to a pristine tree.

## `0001-ingenic-add-t-series-soc-support.patch`

1.8 MB, **548 files**. Adds the Ingenic XBurst/XBurst2 T-series - T10, T20, T21,
T23, T30, T31, T32, T33, T40, T41 and A1 - covering `arch/mips/mach-xburst`, the
device trees, 103 `isvp_*` defconfigs, `board/ingenic`, the CGU and DDR drivers,
and the SFC/SDHCI/ethernet/pinctrl/watchdog/audio drivers. Three small generic
MIPS fixes come with it, each required to build the rest; they are listed in the
patch header.

Derived from the Ingenic T-series work in <https://github.com/gtxaspec/u-boot>,
whose T41 support in turn derives from <https://github.com/oskarirauta/u-boot-t41>,
an import of the Ingenic vendor T41 U-Boot.

### It was 20 MB, and why it no longer is

The upstream form of this work is a single 20,778,450-byte patch touching
**7658 files**. It is not "Ingenic support for a release": it is the whole delta
between a release tarball and a fork, so besides Ingenic it rewrites
`dts/upstream` (2531 files), `tools/binman` (765), `arch/arm` (753),
`board/nxp`, `board/toradex` and much else, and advances `PATCHLEVEL` as a side
effect.

Reducing it needed two steps, because neither alone is enough:

1. **Rebase onto the right release.** Against 2026.04 the delta is 7658 files;
   against 2026.07 it is 1664. The fork is not based on either release exactly,
   so a residue remains either way - but 2026.07 is much the closer, and the
   fork's own `Makefile` claims that version.
2. **Keep only the Ingenic-owned paths**, plus the individual hunks of shared
   `Kconfig`/`Makefile`/driver files that mention Ingenic. Hunks belonging to
   other vendors are dropped. That takes 1664 down to 548.

Net effect: **20.8 MB and 7658 files -> 1.8 MB and 548 files.** The trimmed tree
builds `isvp_t41nq_sfcnor_defconfig` cleanly, and the resulting binary performs
the same MMIO writes as the reference build (see below).

Three generic MIPS files are taken whole rather than hunk-filtered, because the
changes are real fixes that do not mention Ingenic and without them the tree does
not build: `asm/u-boot-mips.h` (declare `board_init()`), `asm/regdef.h` (confine
the MIPS register aliases to assembly so they stop poisoning C identifiers such
as `struct usb_request.zero`), and `asm/system.h` (`__FUNCTION__` -> `__func__`).

## `9998-isvp-t41-add-a-built-in-default-environment.patch`

Written for this port. Adds `board/ingenic/isvp-t41/isvp-t41nq.env`, the
environment compiled into the bootloader, so a board whose environment sector is
blank or corrupt still boots - there is no Linux at that point, and so no
`fw_setenv` to repair it with.

It is installed with `CONFIG_ENV_USE_DEFAULT_ENV_TEXT_FILE`, which **replaces**
U-Boot's generated default environment rather than adding to it. The generated
one would also ship `bootdelay`, `loadaddr`, `mtdids` and `usb_ignorelist`, the
last two as empty strings, none of which this board uses. Dropping `bootdelay`
and `baudrate` is safe: `common/autoboot.c` and `common/board_f.c` fall back to
their Kconfig values.

Note the two arms of that option process the file differently -
`ENV_SOURCE_FILE` runs it through cpp, `ENV_USE_DEFAULT_ENV_TEXT_FILE` only
strips `#` and blank lines - so the file uses `#` comments.

Buildroot's own `BR2_TARGET_UBOOT_DEFAULT_ENV_FILE` is deliberately not used: it
does not exist in buildroot 2024.02.10, and it takes an absolute path - when
that path is a generated file the environment is silently dropped on the next
clean build. That is not hypothetical; it happened during this port's
development, and the only thing that catches it is reading the strings back out
of the binary.

Nothing user-, network- or unit-specific is included. `ethaddr` in particular
must be absent: `CONFIG_XBURST_MAC_FROM_EFUSE` derives a stable per-unit MAC
from the SoC's eFUSE chip serial, using the same derivation as Linux, and it
yields to an environment that already provides one.

## `9999-t41-vendor-pad-init.patch`

Written for this port. Two things, both specific to the Ingenic T41 board this
port was developed on:

- adds `CONFIG_T41_VENDOR_PAD_INIT`, which replays the pad-function and CPM
  USB-PHY register writes the stock vendor SPL performs before DDR init. Without
  them the on-board RTL8733BU drops off the USB bus about two seconds after
  enumeration and the SD slot never completes ACMD41. U-Boot itself boots fine
  either way, which is what makes the omission easy to miss - the symptoms land
  in Linux, a layer away from the cause. Found by disassembling the binary
  running on the camera and comparing it against a build of the tree.
- corrects six entries of the DDR `par[19]` efuse drive/ODT/skew array in
  `arch/mips/dts/t41nq-isvp.dts`.

The option is `default n` and is selected only by this board's config fragment,
so no other board changes behaviour.

## How the build is verified

The toolchain here (gcc 13.3.0) differs from the one the reference build used
(gcc 15.3.0), so the binaries cannot be compared byte for byte. What is compared
instead is the set of MMIO stores each build performs, reconstructed from the
disassembly - the addresses and values the hardware actually sees, which is
compiler independent:

    t41_vendor_cpm_state()   22 stores   u-boot proper
    board_init_f()           16 stores   SPL

Both match the reference exactly. The built-in environment is checked by reading
the `default_environment` symbol back out of the binary rather than by trusting
the build.
