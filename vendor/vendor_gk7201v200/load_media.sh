#! /bin/sh

cd /mnt/mtd/ipc/modules
/mnt/mtd/ipc/modules/load xm72010200 -i -sensor0 sc2336
insmod /mnt/mtd/ipc/modules/xm_wdt.ko
insmod /mnt/mtd/ipc/modules/xm_adc.ko

#ADC
gkmm 0x120C0000 0x1001
#Audio Out
gkmm 0x120C0010 0x1e02
#IRCut
gkmm 0x120C0014 0x1e02
