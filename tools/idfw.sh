#!/bin/sh
# idfw.sh - identify the SoC, sensor and radio of a XiongMai/Goke camera from a raw flash dump.
#
#     sh idfw.sh <dump.bin> [<second-dump.bin>]
#
# Run it on a host, not on the camera. With two dumps it prints both reports so they can be
# compared line by line.
#
# WHY THIS WORKS WITHOUT UNPACKING ANYTHING. The rootfs is compressed, but two things in these
# images are not: the device tree appended to the kernel, and a good part of the jffs2 (small
# files are stored uncompressed). The DTB carries the SoC identity as plain text - a `model`
# line and `compatible` strings - which is authoritative: it is what the kernel itself binds to.
# The jffs2 leaks the sensor name and the radio driver names from the vendor's own load scripts.
# Validated against a known GK7201V200 dump, where it reports xm72010200 / sc2336 / 8733bu.
#
# NOTE the vendor's chip id is not the Goke marketing name: xm72010200 IS GK7201V200. Compare
# the id, not the name.

set -e

report() {
	f=$1
	[ -f "$f" ] || { echo "no such file: $f" >&2; exit 1; }

	echo "=================================================================="
	echo "FILE   : $f"
	echo "size   : $(stat -c %s "$f") bytes    md5 $(md5sum "$f" | cut -d' ' -f1)"

	echo
	echo "-- SoC (from the device tree; this is the authoritative one) ------"
	strings "$f" | grep -oE "xmedia XM[0-9]+ [A-Za-z ]*Board" | sort -u || true
	strings "$f" | grep -oE "xmedia,xm[0-9]{8}(-[a-z]+)?" | sort -u || true
	strings "$f" | grep -oE "\bxm7[0-9]{7}\b" | sort -u | sed 's/^/chip id: /' || true
	strings "$f" | grep -oiE "\bgk7[0-9]{3}v[0-9]{3}\b" | sort -u | sed 's/^/goke name: /' || true

	echo
	echo "-- boot configuration --------------------------------------------"
	strings "$f" | grep -oE "mem=[0-9]+M[^\"']*mtdparts=[^\"' ]*" | sort -u | head -4 || true

	echo
	echo "-- kernel / bootloader -------------------------------------------"
	strings "$f" | grep -oE "Linux version [0-9][^ ]*" | sort -u | head -2 || true
	strings "$f" | grep -oE "U-Boot [0-9]{4}\.[0-9]+[^ ]*" | sort -u | head -2 || true

	echo
	echo "-- image sensor --------------------------------------------------"
	found=0
	for s in sc2336 sc2335 sc2331 sc2232 sc2315 sc2310 sc3235 sc3336 sc4236 sc4336 \
		 sc5239 sc200ai sc223a sc401ai imx307 imx335 imx335p gc2053 gc4653 \
		 os03b10 os05a10 jxf23 jxf37 jxq03; do
		n=$(strings "$f" | grep -ci "$s" || true)
		[ "$n" -gt 0 ] && { echo "  $s ($n hits)"; found=1; }
	done
	[ "$found" = 0 ] && echo "  (none of the known names found - the rootfs may be fully compressed)"

	echo
	echo "-- radio ---------------------------------------------------------"
	found=0
	for w in 8733bu 8723bu 8821cu 8811cu 8188fu 8188eu 8189es 8192eu mt7601 ZT9101 ws73 rtl8723; do
		n=$(strings "$f" | grep -ci "$w" || true)
		[ "$n" -gt 0 ] && { echo "  $w ($n hits)"; found=1; }
	done
	[ "$found" = 0 ] && echo "  (none found)"
	echo "  NOTE: the vendor's load_drv.sh probes several radios on every camera, so more than"
	echo "  one name here is normal. Only the module actually present on the USB bus matters."

	echo
	echo "-- board peripherals mentioned -----------------------------------"
	for p in wifien ircut lsadc relay rled motor ptz speaker; do
		n=$(strings "$f" | grep -ci "$p" || true)
		[ "$n" -gt 0 ] && echo "  $p ($n hits)"
	done

	echo
	echo "-- vendor module load line (names the chip AND the sensor) -------"
	strings "$f" | grep -oE "load +xm[0-9]{8}[^\"']*" | sort -u | head -4 || true
	echo "=================================================================="
	echo
}

[ $# -ge 1 ] || { echo "usage: sh $0 <dump.bin> [<second-dump.bin>]" >&2; exit 1; }
for f in "$@"; do report "$f"; done
