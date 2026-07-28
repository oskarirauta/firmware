#!/bin/sh
# load_xm.sh — load the VENDOR-native GK7201V200 (xm72010200) MPP kernel modules
# under OpenIPC, replacing load_goke. Derived verbatim from the stock firmware's
# /mnt/mtd/ipc/modules/load sequence for chip xm72010200 + sensor sc2336.
#
# Rationale: the gk7205v200-built OpenIPC modules bring the SoC up to the ISP but
# fail VEDU CreateChn (F008FFFF) and never start the VI->VPSS online link on the
# 7201 die. The vendor xm_*.ko (vermagic 4.9.37, MPP V1.0.1.0 B020 — ABI-matches
# OpenIPC libmpi) drive this exact silicon correctly.
#
# Prereqs on target:
#   - vendor xm_*.ko in $MOD_DIR (below), extdrv/sinit.ko present
#   - busybox 'devmem' available (replaces vendor 'gkmm')
#   - kernel bootargs must reserve OS mem to match the MMZ below:
#         mem=47M   (OS 0x40000000..0x42F00000, MMZ 0x42F00000..0x44000000 = 17M)
#     i.e. set 'mem=47M' in bootargs (vendor value); do NOT leave OpenIPC's mem=32M.
#   - Majestic sensor handling: let the vendor sensor path own the sensor
#     (xm_isp_sensor_i2c + conf/sc2336p.bin); see NOTE at end.

set -e
MOD_DIR=/lib/modules/4.9.37/xm         # where the vendor xm_*.ko live on target
CONF_DIR=/etc/sensors                  # where sc2336p.bin is placed

CHIP_TYPE=xm72010200
SNS_TYPE0=sc2336
YUV_TYPE0=0                            # 0 = raw
BOARD=demo

# --- MMZ (vendor xm72010200 values) ---
MMZ_START=0x42F00000
MMZ_SIZE=17M

cd "$MOD_DIR"

reg() { devmem "$1" 32 "$2"; }        # reg <phys_addr> <value>  (== vendor 'gkmm')

echo "load_xm: chip=$CHIP_TYPE sensor=$SNS_TYPE0 mmz=$MMZ_START/$MMZ_SIZE"

# --- audio (optional; keep for parity with stock, harmless) ---
insmod xm_aio.ko    2>/dev/null || true
insmod xm_ai.ko     2>/dev/null || true
insmod xm_ao.ko     2>/dev/null || true
insmod xm_aenc.ko   2>/dev/null || true
insmod xm_adec.ko   2>/dev/null || true
insmod xm_acodec.ko 2>/dev/null || true

# --- core: osal (with MMZ) + sysconfig (chip + sensor) ---
insmod xm_osal.ko mmz_allocator=xmedia mmz=anonymous,0,${MMZ_START},${MMZ_SIZE}
insmod xm_sysconfig.ko chip=${CHIP_TYPE} sensors=${SNS_TYPE0} g_cmos_yuv_flag=${YUV_TYPE0} board=${BOARD}

# --- sensor pinmux / MCLK (vendor 'gkmm' writes; sensor assumed present) ---
reg 0x112C002C 0x1000
reg 0x120100F0 0x0D
insmod extdrv/sinit.ko 2>/dev/null || true
reg 0x120100F0 0x19          # vendor sets this once a sensor is detected

# --- media stack (exact vendor order) ---
insmod xm_base.ko
insmod xm_sys.ko
insmod xm_rgn.ko
insmod xm_pm.ko    2>/dev/null || true
insmod xm_vgs.ko
insmod xm_vi.ko
insmod xm_isp.ko
insmod xm_vpss.ko
insmod xm_chnl.ko
insmod xm_vedu.ko            # <-- the native H.264/H.265 encoder for this die
insmod xm_rc.ko
insmod xm_venc.ko
insmod xm_h264e.ko
insmod xm_h265e.ko
insmod xm_jpege.ko
insmod xm_ive.ko save_power=0 max_node_num=20
insmod xm_isp_pwm.ko          2>/dev/null || true
insmod xm_isp_sensor_i2c.ko
insmod xm_isp_sensor_spi.ko   2>/dev/null || true
insmod xm_mipi_rx.ko
insmod xm_pdm.ko              2>/dev/null || true

echo "load_xm: done. /dev nodes:"; ls -1 /dev | grep -iE 'sys|venc|vpss|vi$|isp|mipi|region' || true

# NOTE (Majestic integration — resolve on-device):
#   Majestic normally loads its own libsns_*.so and configures VI. Here the sensor
#   must be driven by the vendor path with conf/sc2336p.bin (NOT Majestic's .bin
#   loader). Two options to try:
#     1) point Majestic at the vendor bin:  cli -s .isp.sensorConfig ${CONF_DIR}/sc2336p.bin
#     2) if Majestic's sensor init conflicts with the vendor modules, disable
#        Majestic's sensor bring-up and let xm_isp_sensor_i2c + sc2336p.bin own it.
#   After load: start Majestic, then check  /proc/umap/vpss (RecvPic0 should climb)
#   and the Majestic log (VENC chn 0 should create — no F008FFFF).
