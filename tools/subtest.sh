#!/bin/sh
# subtest.sh - the three follow-up tests the substream fix left open.
#
#     sh /mnt/mmcblk0p1/subtest.sh state                    what is on the camera now
#     sh /mnt/mmcblk0p1/subtest.sh install <shim.so>        install the shim to test with
#     sh /mnt/mmcblk0p1/subtest.sh go                       run the cycle (once per boot)
#     sh /mnt/mmcblk0p1/subtest.sh done                     put the camera back to normal
#
# Each 'go' prints the report for the case that was armed before the last reboot, then arms
# the next case and reboots. So the cycle is: go -> (reboot) -> go -> paste -> ... -> done.
#
# Cases:
#   A1  no GK_VIVPSS_MODE (majestic's own online/online), substream 640x360  -> is offline still needed?
#   A2  no GK_VIVPSS_MODE, substream 1920x1080 (same size as the group)      -> is it scaling or channels?
#   A3  GK_VIVPSS_MODE=2 (offline), substream 640x360                        -> the known-good control
#   R0  no GK_VIVPSS_MODE, substream 640x360, image.rotate 90                -> rotation as shipped
#   R2  GK_VIVPSS_MODE=2, substream 640x360, image.rotate 90                 -> rotation, offline
#
# RESULT SO FAR: A1 and A2 both pass in majestic's own VI_ONLINE_VPSS_ONLINE mode - three
# channels, scaled AND same-size, zero venc timeouts. GK_VIVPSS_MODE=2 is therefore NOT needed.
#
# Every case dumps /proc/umap/vpss TWICE, 15 s apart, so the counters can be seen climbing
# rather than guessed from one snapshot. That also answers the offline frame-accounting
# question (RecvPic / NewUnDo / CostTm) for both modes.
#
# WHICH SHIM GETS TESTED. /tmp is tmpfs and this script reboots between cases, so a shim in
# /tmp would vanish at the first reboot. 'install' therefore stashes it on the SD card and
# copies it to /usr/lib/libgk_shim.so, and every 'go' restores that copy from the stash before
# starting majestic. /usr/lib is on the writable overlay, so this is persistent and reversible:
# the squashfs original is still visible at /rom/usr/lib/libgk_shim.so and 'done' puts it back.
# TRAP WORTH KNOWING: an overlay copy MASKS the flashed one. If the rootfs is reflashed while a
# hand-built shim sits in /usr/lib, the camera keeps running the old hand-built file and the new
# image looks like it changed nothing. Always run 'done' (or cp from /rom) before flashing.
#
# Safety: majestic's watchdog is forced off for the whole run (a broken case must not
# reboot-loop an assembled camera), and S95majestic is moved aside so majestic only ever runs
# once per boot - started by this script, with its stderr captured.

SD=/mnt/mmcblk0p1
STATE=$SD/subtest.state
STASH=$SD/libgk_shim.test.so
SHIM=/usr/lib/libgk_shim.so
ROM=/rom/usr/lib/libgk_shim.so
SETTLE=20        # seconds after majestic starts before the first sample
GAP=15           # seconds between the two samples

die() { echo "subtest: $*" >&2; exit 1; }

[ -d $SD ] || die "no $SD - is the SD card mounted?"

# ---------------------------------------------------------------- helpers

# The one check that matters, and the only one that does not depend on compiler flags:
# the fixed shim must NOT carry 0x40045024 in its probe list, and must carry 0x40044977.
shim_verdict() {
	f=$1
	[ -f "$f" ] || { echo "(absent)"; return; }
	# v2 carries a self-describing marker; v1 (the first fixed build) has to be recognised by
	# its probe list, which was still a string back then. Anything that still carries
	# 0x40045024 is pre-fix and must never be installed.
	id=$(strings "$f" | grep -m1 "^gk-shim:")
	bad=$(strings "$f" | grep -c 0x40045024)
	if [ -n "$id" ]; then
		echo "FIXED [$id]"
	elif [ "$bad" != 0 ]; then
		echo "*** PRE-FIX (still swallows 0x40045024) ***"
	elif [ "$(strings "$f" | grep -c 0x40044977)" -ge 1 ]; then
		echo "FIXED (v1, no marker)"
	else
		echo "*** UNRECOGNISED ***"
	fi
}

shim_line() {
	f=$1
	if [ -f "$f" ]; then
		echo "$f: $(md5sum "$f" | cut -d' ' -f1)  $(stat -c %s "$f") B  $(shim_verdict "$f")"
	else
		echo "$f: (absent)"
	fi
}

stop_majestic() {
	killall -q majestic 2>/dev/null
	sleep 2
	killall -q -9 majestic 2>/dev/null
	sleep 1
}

# majestic must not autostart: every case has to measure the FIRST majestic run after the boot,
# in the mode the case asks for. mv can fail on this overlay ("Invalid argument"), so fall back
# to copy+remove and finally to dropping the exec bit - rcS runs "$i start", which a
# non-executable file simply refuses. 'done' undoes whichever one took effect.
park_init() {
	[ -f /etc/init.d/S95majestic ] || return 0
	mkdir -p /root/init.d
	if mv /etc/init.d/S95majestic /etc/init.d/_S95majestic 2>/dev/null; then
		echo "subtest: parked S95majestic (renamed)"
	elif cp /etc/init.d/S95majestic /root/init.d/S95majestic 2>/dev/null &&
	     rm -f /etc/init.d/S95majestic; then
		echo "subtest: parked S95majestic (copied to /root/init.d, removed from /etc/init.d)"
	elif chmod -x /etc/init.d/S95majestic 2>/dev/null; then
		echo "subtest: parked S95majestic (exec bit cleared)"
	else
		echo "subtest: WARNING could not park S95majestic - majestic will autostart" >&2
	fi
}

# Put the stashed shim back after the reboot wiped nothing but proving it every time is cheap.
sync_shim() {
	[ -f $STASH ] || die "no $STASH - run 'sh $0 install <shim.so>' first"
	if [ "$(md5sum $STASH | cut -d' ' -f1)" != "$(md5sum $SHIM 2>/dev/null | cut -d' ' -f1)" ]; then
		cp $STASH $SHIM
		echo "subtest: refreshed $SHIM from the stash"
	fi
}

# arm <case> <mode|-> <size> <rotate>
arm() {
	c=$1; mode=$2; size=$3; rot=$4
	park_init
	stop_majestic
	cli -s .watchdog.enabled false
	cli -s .video1.enabled true
	cli -s .video1.size "$size"
	cli -s .image.rotate "$rot"
	echo "$c $mode $size $rot" > $STATE
	sync
	echo
	echo "subtest: armed case $c  (mode=$mode size=$size rotate=$rot). Rebooting."
	echo "subtest: after it comes back, run:  sh $SD/subtest.sh go"
	sleep 2
	reboot
}

# measure <case> <mode> <size> <rotate>
measure() {
	c=$1; mode=$2; size=$3; rot=$4
	log=$SD/subtest-$c.log

	stop_majestic
	sync_shim
	rm -f "$log"

	if [ "$mode" = "-" ]; then
		LD_PRELOAD=$SHIM majestic -s > "$log" 2>&1 &
	else
		LD_PRELOAD=$SHIM GK_VIVPSS_MODE=$mode majestic -s > "$log" 2>&1 &
	fi

	sleep $SETTLE

	echo "=============================================================="
	echo "CASE $c   mode=$mode  video1=$size  rotate=$rot"
	echo "shim     : $(md5sum $SHIM | cut -d' ' -f1)  $(shim_verdict $SHIM)"
	echo "uptime   : $(cut -d' ' -f1 /proc/uptime) s   oops=$(dmesg | grep -ci oops)"
	echo "majestic : $(pgrep majestic | tr '\n' ' ')"
	echo "-------- /proc/umap/vpss  SAMPLE 1 ---------------------------"
	cat /proc/umap/vpss
	sleep $GAP
	echo "-------- /proc/umap/vpss  SAMPLE 2 (+${GAP}s) ----------------"
	cat /proc/umap/vpss
	echo "-------- /proc/umap/venc -------------------------------------"
	head -60 /proc/umap/venc
	echo "-------- majestic log: interesting lines ---------------------"
	grep -inE "mode|error|cannot|fail|illegal|timeout|rotat|venc|vpss" "$log" | head -40
	echo "-------- counts ----------------------------------------------"
	echo "venc timeouts : $(grep -c 'Timeout from venc' "$log")"
	echo "log lines     : $(wc -l < "$log")   (full log: $log)"
	echo "=============================================================="
	echo

	stop_majestic
}

# ---------------------------------------------------------------- main

case "$1" in
install)
	src=$2
	[ -f "$src" ] || die "usage: sh $0 install <path to libgk_shim.so>"
	v=$(shim_verdict "$src")
	echo "source   : $src  $(md5sum "$src" | cut -d' ' -f1)  $(stat -c %s "$src") B  $v"
	case "$v" in
	FIXED*) ;;
	*) die "refusing to install: that shim is $v" ;;
	esac
	cp "$src" $STASH
	[ "$src" = "$SHIM" ] || cp "$src" $SHIM
	sync
	echo "subtest: stashed at $STASH and installed as $SHIM"
	echo "subtest: 'done' will restore the squashfs original from $ROM"
	;;

park)
	# Only needed to repair a boot where parking failed and majestic autostarted anyway.
	park_init
	sync
	echo "subtest: rebooting so the armed case gets a clean first majestic run."
	sleep 2
	reboot
	;;

go)
	if [ -f $STATE ]; then
		read c mode size rot < $STATE
		measure "$c" "$mode" "$size" "$rot"
	else
		[ -f $STASH ] || die "no $STASH - run 'sh $0 install <shim.so>' first"
		c=none
	fi

	case "$c" in
	none) arm A1 -  640x360   0 ;;
	A1)   arm A2 -  1920x1080 0 ;;
	A2)   arm A3 2  640x360   0 ;;
	# A1 and A2 both passed in majestic's own online mode, so the shipping configuration has
	# NO mode override - rotation has to be tried there first (R0). R2 repeats it in offline
	# mode only to settle whether the mode makes any difference to rotation at all.
	A3)   arm R0 -  640x360   90 ;;
	R0)   arm R2 2  640x360   90 ;;
	R2)
		rm -f $STATE
		echo "subtest: all four cases done. Paste the reports above."
		echo "subtest: run 'sh $SD/subtest.sh done' to put the camera back to normal."
		;;
	*)    die "unknown case '$c' in $STATE" ;;
	esac
	;;

done)
	stop_majestic
	rm -f $STATE
	cli -s .image.rotate 0
	cli -s .video1.enabled true
	cli -s .video1.size 640x360
	cli -s .watchdog.enabled true
	# The shim is deliberately NOT restored from /rom here: /rom still holds the PRE-FIX one
	# until the new rootfs is flashed, and putting it back would take the substream away again.
	# Restore it (cp $ROM $SHIM) only immediately before flashing - see the header.
	for f in /etc/init.d/_S95majestic /root/init.d/S95majestic /root/init.d/_S95majestic; do
		[ -f "$f" ] && { cp "$f" /etc/init.d/S95majestic; chmod +x /etc/init.d/S95majestic; \
			echo "subtest: restored /etc/init.d/S95majestic from $f"; break; }
	done
	[ -f /etc/init.d/S95majestic ] || echo "subtest: WARNING no S95majestic found to restore"
	sync
	echo "subtest: restored (rotate 0, substream 640x360, watchdog on, init script back)."
	echo "subtest: the shim in /usr/lib is left as-is - it is the only FIXED copy on the box:"
	shim_line $SHIM
	shim_line $ROM
	echo "subtest: rebooting into normal operation."
	sleep 2
	reboot
	;;

state)
	echo "state file : $(cat $STATE 2>/dev/null || echo '(none)')"
	echo "shims:"
	for f in $SHIM $ROM $STASH /tmp/libgk_shim.so; do echo "  $(shim_line $f)"; done
	echo "init script: $(ls /etc/init.d/ | grep -i majestic | tr '\n' ' ')"
	echo "watchdog   : $(cli -g .watchdog.enabled)"
	echo "video1     : $(cli -g .video1.enabled) $(cli -g .video1.size)"
	echo "rotate     : $(cli -g .image.rotate)"
	echo "majestic   : $(pgrep majestic | tr '\n' ' ')"
	echo "mtd/rootfs : $(grep -c . /proc/mtd) partitions; overlay $(df -h /overlay 2>/dev/null | tail -1)"
	;;

*)
	echo "usage: sh $0 {state|install <shim.so>|go|park|done}"
	exit 1
	;;
esac
