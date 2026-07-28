#!/bin/sh
# apply_v500_gk7201v200.sh - close the six gaps of v12 §7.1 in the buildroot tree.
#
# Moves this camera from the Aug 2021 gk7205v200 stack to the Apr 2023 gk7205v500 stack.
# Idempotent: safe to re-run. Every change is reported. Nothing is silent.
#
# WHAT IT DOES NOT DO
#   - does not touch the defconfig (§7.2 - do that by hand, it is 6 lines and it deserves eyes)
#   - does not build
#   - does not flash
#
# INPUTS
#   $VBITS   directory holding the vendor bits extracted from vendor_gk7201v200_stack.tar.gz:
#              xm_sysconfig.ko   (bf6324580f3132fc3294bd225626958a)  - knows xm72010200
#              sc2336p.bin       (37670c962b18dafce17e16ca01b9f23f)  - the SC2336P IQ
#              load_xm.sh                                            - the collaborator's loader
#   $SHIMS   directory holding the six generated libhi_*.so (default /tmp/xmshim2/lib)
#   $FW      buildroot tree (default: cwd)
#
# USAGE
#   cd /work/firmware
#   VBITS=/tmp/vendor_bits sh apply_v500_gk7201v200.sh
#
# Then edit the defconfig per v12 §7.2 and build.

set -u

FW=${FW:-$(pwd)}
VBITS=${VBITS:-/tmp/vendor_bits}
SHIMS=${SHIMS:-/tmp/xmshim2/lib}
PKG=$FW/general/package/goke-osdrv-gk7205v500
MK=$PKG/goke-osdrv-gk7205v500.mk
BAK=$FW/.v500-backup

say()  { printf '  %s\n' "$*"; }
ok()   { printf '  [ok]   %s\n' "$*"; }
skip() { printf '  [skip] %s\n' "$*"; }
die()  { printf '  [ERR]  %s\n' "$*"; exit 1; }

[ -f "$MK" ]  || die "no $MK - wrong tree?"
[ -d "$VBITS" ] || die "no VBITS dir $VBITS"
[ -d "$SHIMS" ] || die "no SHIMS dir $SHIMS - run gen_xmedia_shim2.sh first"

mkdir -p "$BAK"

echo "=== gap 3: xm_sysconfig.ko that knows xm72010200 ==="
# The osdrv module's chip_list has 9 chips; the vendor's has 11. The two missing are
# xm72010200 and xm72010300. Same vermagic (4.9.37 ARMv7 thumb2 p2v8), same five params,
# same depends=xm_osal. chip=gk7201v200 matches nothing -> pinmux and sensor clock never set.
if [ ! -f "$VBITS/xm_sysconfig.ko" ]; then
    die "no $VBITS/xm_sysconfig.ko"
fi
if ! strings "$VBITS/xm_sysconfig.ko" | grep -qx 'xm72010200'; then
    die "$VBITS/xm_sysconfig.ko does NOT contain xm72010200 - wrong file"
fi
if [ ! -f "$BAK/xm_sysconfig.ko.osdrv" ]; then
    cp "$PKG/files/kmod/xm_sysconfig.ko" "$BAK/xm_sysconfig.ko.osdrv"
    say "osdrv original saved to $BAK/xm_sysconfig.ko.osdrv"
fi
cp "$VBITS/xm_sysconfig.ko" "$PKG/files/kmod/xm_sysconfig.ko"
ok "vendor xm_sysconfig.ko installed ($(md5sum "$PKG/files/kmod/xm_sysconfig.ko" | cut -d' ' -f1))"

echo
echo "=== gap 1: sc2336_i2c_1080p.ini into the package ==="
# The .mk installs files/sensor/config/*.ini -> /etc/sensors/. The hand-tuned ini
# (ac3638e1c6f92e84b9a69c9f0575be5d, DevRect_x=200) lives at the repo root and never
# made it into the package.
SRC_INI=""
for c in "$FW/sc2336_i2c_1080p.ini" "$VBITS/sc2336_i2c_1080p.ini"; do
    [ -f "$c" ] && { SRC_INI=$c; break; }
done
if [ -n "$SRC_INI" ]; then
    cp "$SRC_INI" "$PKG/files/sensor/config/sc2336_i2c_1080p.ini"
    ok "sc2336_i2c_1080p.ini <- $SRC_INI  ($(md5sum "$SRC_INI" | cut -d' ' -f1))"
else
    die "sc2336_i2c_1080p.ini not found at repo root or in VBITS"
fi

echo
echo "=== gap 2: SC2336P IQ ==="
# Without this, .mk symlinks default.ini -> sc2232.ini and SC2336 runs on SC2232's IQ.
# That is an ugly picture, not an absent one. Not a blocker, but it is free.
if [ -f "$VBITS/sc2336p.bin" ]; then
    mkdir -p "$PKG/files/sensor/iq"
    cp "$VBITS/sc2336p.bin" "$PKG/files/sensor/iq/sc2336p.bin"
    ok "sc2336p.bin installed ($(md5sum "$VBITS/sc2336p.bin" | cut -d' ' -f1))"
else
    skip "no sc2336p.bin in VBITS - SC2336 will run on sc2232's IQ (ugly, not fatal)"
fi

echo
echo "=== gap 4: the six libhi_*.so facades ==="
# HISILICON_OPENSDK goes off, so nothing else provides libhi_*.so.
# WITHOUT THESE MAJESTIC DOES NOT START AT ALL.
n=0
for h in mpi ae awb isp ive md; do
    if [ -f "$SHIMS/libhi_${h}.so" ]; then
        cp "$SHIMS/libhi_${h}.so" "$PKG/files/lib/libhi_${h}.so"
        n=$((n + 1))
    else
        die "missing $SHIMS/libhi_${h}.so - run gen_xmedia_shim2.sh"
    fi
done
ok "$n/6 shims copied into files/lib/"

echo
echo "=== .mk install lines ==="
# All INSTALL lines live inside a single define...endef. Insert before endef.
# Marker keeps this idempotent.
if grep -q 'GK7201V200-V500-PATCH' "$MK"; then
    skip ".mk already patched"
else
    cp "$MK" "$BAK/goke-osdrv-gk7205v500.mk.orig"
    TMP=$(mktemp)
    awk '
      /^endef/ && !done {
        printf "\t# ---- GK7201V200-V500-PATCH: shims + SC2336P IQ ----\n"
        split("mpi ae awb isp ive md", a, " ")
        for (i = 1; i <= 6; i++)
          printf "\t$(INSTALL) -m 644 -t $(TARGET_DIR)/usr/lib $(GOKE_OSDRV_GK7205V500_PKGDIR)/files/lib/libhi_%s.so\n", a[i]
        printf "\t$(INSTALL) -m 644 -t $(TARGET_DIR)/etc/sensors/iq $(GOKE_OSDRV_GK7205V500_PKGDIR)/files/sensor/iq/sc2336p.bin\n"
        done = 1
      }
      { print }
    ' "$MK" > "$TMP" && mv "$TMP" "$MK"
    ok ".mk patched (original in $BAK/)"
fi

echo
echo "=== gap 5: load_xm.sh ==="
# load_goke does: chip=$(ipcinfo --chip-name) -> "gk7201v200" -> matches no chip_list entry.
# load_xm.sh passes chip=xm72010200 and the vendor's exact insmod order.
if [ -f "$VBITS/load_xm.sh" ]; then
    mkdir -p "$PKG/files/script"
    sed 's#^MOD_DIR=.*#MOD_DIR=/lib/modules/4.9.37/goke         # .mk installs here, not /xm#' \
        "$VBITS/load_xm.sh" > "$PKG/files/script/load_xm"
    chmod 755 "$PKG/files/script/load_xm"
    ok "load_xm installed into files/script/ (MOD_DIR retargeted to .../goke)"
    say "NOTE: .mk already installs files/script/load* -> /usr/bin, so it ships automatically."
    say "NOTE: load_xm.sh insmods extdrv/sinit.ko, which is NOT in the osdrv package."
    say "      Its line is '|| true' tolerant and the 0x120100F0=0x19 write is unconditional,"
    say "      so this should be survivable. If the sensor does not come up, sinit.ko is in"
    say "      the vendor tarball at modules/extdrv/sinit.ko - that is gap 7, see below."
else
    skip "no load_xm.sh in VBITS"
fi

echo
echo "================================ REMAINING, BY HAND ================================"
cat <<'EOF'

  gap 6  bootargs: mem=47M       <- NOT optional
         load's check_mem_info() reads /proc/cmdline and EXITS on mismatch.
         MMZ 0x42F00000 + 17M is where the vendor's VB Pool0 (0x42f61000) lives.
         OpenIPC's mem=32M puts MMZ at 0x42000000. Different map.
             setenv bootargs '... mem=47M ...'

  §7.2   defconfig - by hand, six lines:
             SOC_FAMILY        = gk7205v500
             KERNEL_CONFIG     = board/gk7205v500/gk7201v200.generic.config
             OSDRV             = GK7205V500
             HISILICON_OPENSDK = off        # unset it
             MAJESTIC          = y
             FLASH_SIZE        = 16
         i.e. upstream's own defconfig + Majestic + 16MB. Revert v3 §4.2's three lines.

  gap 7  extdrv/sinit.ko is not in the osdrv package (vendor tarball only).
         Deferred: load_xm tolerates its absence. Revisit only if the sensor is silent.

  DROP   venc_fix.ko venc_fix2.ko rc_fix.ko open_sys_config_fix.ko vpss_onebuf*.ko
         securec_shim.so libsns_sc2336.so.patched
         -> all patch gk7205v200_*.ko modules that will not exist, or route around a
            stack that is gone.

  KEEP   vpss_lowdelay.so - userspace, reaches XMEDIA through the facade. See below.

EOF

echo "=================================== VERIFY ==================================="
echo
printf 'vendor xm_sysconfig knows xm72010200 : '
strings "$PKG/files/kmod/xm_sysconfig.ko" | grep -qx 'xm72010200' && echo YES || echo "NO  <-- STOP"
printf 'sc2336 ini in package               : '
[ -f "$PKG/files/sensor/config/sc2336_i2c_1080p.ini" ] && echo YES || echo NO
printf 'sc2336p.bin in package              : '
[ -f "$PKG/files/sensor/iq/sc2336p.bin" ] && echo YES || echo "no (ugly IQ, not fatal)"
printf 'six libhi_*.so in package           : '
echo "$(ls "$PKG"/files/lib/libhi_*.so 2>/dev/null | wc -l)/6"
printf 'load_xm in package                  : '
[ -f "$PKG/files/script/load_xm" ] && echo YES || echo NO
printf '.mk patched                         : '
grep -q 'GK7201V200-V500-PATCH' "$MK" && echo YES || echo NO
echo
echo "Backups: $BAK"
echo
echo "Next: defconfig (§7.2), build, flash with mem=47M, boot."
echo "Three questions get answered by that boot and nothing else:"
echo "  1. do the xm_*.ko load?            -> vermagic (v12 §2.1)"
echo "  2. does majestic link?             -> struct compat (v12 §6.5)"
echo "  3. does chn0 produce wrap output?  -> bug 2/7 (v12 §3.1, BUG7_MECHANISM §5)"
