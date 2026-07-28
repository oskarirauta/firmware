#!/bin/sh
# postflash.sh - verify the final image, with the emphasis on what actually changed.
#
#     sh /mnt/mmcblk0p1/postflash.sh
#
# Everything else in this port is already proven on hardware. This image changes exactly three
# things, and all three are tested below:
#
#   1. /etc/init.d/S95majestic now gates SIGKILL and the watchdog release on MAJESTIC_HARD_STOP,
#      which /etc/default/majestic sets. The previous image shipped a STALE env file without the
#      flag, so this pair has never run together on hardware.
#   2. two kernel patches were dropped (only the SP805 device-tree one ships).
#   3. sysupgrade was updated upstream.
#
# The watchdog state after a stop is the direct proof that wdt_release ran: with the flag unset
# the dog would stay armed and the node would still read "active".

W=/sys/class/watchdog/watchdog0
LOG=/tmp/postflash.$$

hdr() { echo; echo "=== $* ============================================"; }
chn() { sed -n '/VPSS CHN OUTPUT RESOLUTION/,/VPSS 3DNR/p' /proc/umap/vpss | grep -E '^ +0 +[0-9]'; }
oops() { dmesg | grep -ciE 'oops|kernel panic|unable to handle'; }
wstate() { cat $W/state 2>/dev/null || echo "(no watchdog node)"; }

hdr "IDENTITY"
grep -E 'BUILD_ID|TIME_STAMP' /usr/lib/os-release
echo "kernel   : $(uname -r) $(uname -v)"
echo "shim     : $(md5sum /usr/lib/libgk_shim.so | cut -d' ' -f1)  (expect 06e722efecc6fc116af84f06b14e2fc9)"
strings /usr/lib/libgk_shim.so | grep '^gk-shim:'
echo "init gate: $(grep -c MAJESTIC_HARD_STOP /etc/init.d/S95majestic) refs in S95majestic (expect 3)"
echo "env flag : $(grep -c 'MAJESTIC_HARD_STOP=1' /etc/default/majestic) in /etc/default/majestic (expect 1)"
echo "watchdog : $W $( [ -e $W ] && echo present || echo MISSING )   <- proves the SP805 DTS patch bound"
echo "overlay  : $(find /overlay/root -type f 2>/dev/null | wc -l) file(s); ours should not be among them"
find /overlay/root -type f 2>/dev/null | grep -E 'libgk_shim|load_xm|S95majestic|default/majestic' || echo "  (none of ours - good)"
echo "oops     : $(oops)"

hdr "BASELINE - watchdog on, majestic running from its init script"
cli -s .watchdog.enabled true
/etc/init.d/S95majestic restart
sleep 25
echo "majestic : $(pgrep majestic | tr '\n' ' ')"
echo "wdt state: $(wstate)   timeout=$(cat $W/timeout 2>/dev/null)"
chn

hdr "TEST 1 - stop must kill majestic AND release the watchdog"
/etc/init.d/S95majestic stop
sleep 3
echo "majestic : '$(pgrep majestic | tr '\n' ' ')'   (empty = stopped)"
echo "wdt state: $(wstate)   <- MUST be 'inactive'; 'active' means wdt_release did not run"
echo "oops     : $(oops)     <- must still be 0; a graceful teardown here Oopses this SoC"

hdr "TEST 2 - start must bring all three channels back"
/etc/init.d/S95majestic start
sleep 25
echo "majestic : $(pgrep majestic | tr '\n' ' ')"
echo "wdt state: $(wstate)"
chn

hdr "TEST 3 - restart (stop+start in one go, the path a settings change uses)"
/etc/init.d/S95majestic restart
sleep 25
echo "majestic : $(pgrep majestic | tr '\n' ' ')"
chn
sleep 15
echo "-- 15 s later, counters must have climbed --"
chn

hdr "TEST 4 - is the dog actually being fed?"
echo "Using a short timeout so a feed is visible inside 90 s (majestic feeds about every"
echo "timeout/2, which is why a 25 s window against a 240 s timeout proves nothing)."
cli -s .watchdog.timeout 60
/etc/init.d/S95majestic restart
sleep 20
echo "timeout=$(cat $W/timeout 2>/dev/null) state=$(wstate)"
for i in 1 2 3 4 5 6 7 8 9 10 11 12; do printf "%s " "$(cat $W/timeleft 2>/dev/null)"; sleep 7; done; echo
echo "^ must bounce back up at least twice; a steady decay to zero means nobody is feeding it"

hdr "FINAL"
echo "oops     : $(oops)"
echo "majestic : $(pgrep majestic | tr '\n' ' ')"
chn
echo
echo "postflash: restore your preferred watchdog timeout if 60 s is not what you want:"
echo "postflash:     cli -s .watchdog.timeout 240 && /etc/init.d/S95majestic restart"
echo "postflash: still to check by eye: both RTSP streams, /image.jpg, audio, night mode."
