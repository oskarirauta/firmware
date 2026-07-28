#!/bin/sh
# regress.sh - full-feature regression on the freshly flashed image.
#
#     sh /mnt/mmcblk0p1/regress.sh
#
# Checks what can be measured, in the configuration that actually ships: majestic started by
# its own init script, no env, no hand-built anything. It ends with the two tests that used to
# wedge this camera - a settings change with SIGHUP, and a full majestic restart - because those
# are the ones that regress silently.
#
# Not covered here, look at these yourself: the RTSP streams in a player (video0 and video1),
# http://<cam>/image.jpg in a browser, and audio in the main stream.

SD=/mnt/mmcblk0p1
SHIM=/usr/lib/libgk_shim.so

hdr() { echo; echo "=== $* ============================================"; }

# The three VPSS output channels and the VENC send counters, which together prove frames are
# flowing rather than merely configured.
snap() {
	sed -n '/VPSS CHN OUTPUT RESOLUTION/,/VPSS 3DNR/p' /proc/umap/vpss | grep -E '^ +0 +[0-9]'
}
vencsnap() {
	sed -n '/VENC SEND1/,/VENC SEND2/p' /proc/umap/venc | grep -E '^ +[0-9]+ +[0-9]'
}

hdr "IDENTITY"
strings $SHIM | grep '^gk-shim:'
echo "shim md5    : $(md5sum $SHIM | cut -d' ' -f1)   (expect 06e722efecc6fc116af84f06b14e2fc9)"
grep -E 'BUILD_ID|TIME_STAMP|GITHUB_VERSION' /usr/lib/os-release
echo "uptime      : $(cut -d' ' -f1 /proc/uptime) s"
echo "majestic pid: $(pgrep majestic | tr '\n' ' ')  pidfile=$(cat /var/run/majestic.pid 2>/dev/null)"
echo "init script : $(ls -l /etc/init.d/S95majestic 2>/dev/null | awk '{print $1, $NF}')"
echo "overlay     : $(find /overlay/root -type f 2>/dev/null | wc -l) file(s) masking the image"
find /overlay/root -type f 2>/dev/null | head -20

hdr "KERNEL HEALTH"
echo "oops/panic  : $(dmesg | grep -ciE 'oops|kernel panic|unable to handle')"
dmesg | grep -iE 'oops|panic|segfault|nobody cared' | head -5
echo "MMZ:"; cat /proc/umap/mmz 2>/dev/null | head -5

hdr "VIDEO - three channels, two samples 15 s apart"
echo "-- vpss t=0"; snap
echo "-- venc t=0"; vencsnap
sleep 15
echo "-- vpss t=15"; snap
echo "-- venc t=15"; vencsnap

hdr "WIFI"
iwconfig wlan0 2>/dev/null | head -3
ifconfig wlan0 2>/dev/null | sed -n '2p'
echo "default route: $(ip route 2>/dev/null | grep default || route -n | grep '^0.0.0.0')"

hdr "WATCHDOG - enabling it now, then watching timeleft bounce"
cli -s .watchdog.enabled true
/etc/init.d/S95majestic restart
sleep 20
W=/sys/class/watchdog/watchdog0
echo "state=$(cat $W/state 2>/dev/null) timeout=$(cat $W/timeout 2>/dev/null) nowayout=$(cat $W/nowayout 2>/dev/null)"
for i in 1 2 3 4 5 6; do printf "timeleft %s  " "$(cat $W/timeleft 2>/dev/null)"; sleep 5; done; echo
echo "(timeleft must bounce back up - decaying steadily to zero means nobody is feeding it)"

hdr "AFTER THE RESTART - channels must be back"
snap
vencsnap

hdr "SETTINGS CHANGE + SIGHUP - the old wedge test"
cli -s .video0.bitrate 3072
killall -1 majestic
sleep 15
echo "majestic pid: $(pgrep majestic | tr '\n' ' ')"
snap
cli -s .video0.bitrate 4096
killall -1 majestic
sleep 10

hdr "FINAL STATE"
echo "oops/panic  : $(dmesg | grep -ciE 'oops|kernel panic|unable to handle')"
echo "majestic pid: $(pgrep majestic | tr '\n' ' ')"
snap
echo
echo "regress: done. Still to check by hand: RTSP video0 + video1 in a player,"
echo "regress: http://<cam>/image.jpg in a browser, audio in the main stream,"
echo "regress: and night mode (cover the lens and watch the picture go monochrome)."
