#! /bin/sh

DRV_PATH="/mnt/mtd/ipc/modules/extdrv"
WIFIPATH="/mnt/mtd/ipc/conf/wifi.conf"
. $WIFIPATH

loadNFS()
{
	insmod /mnt/mtd/ipc/modules/nfs/sunrpc.ko
	insmod /mnt/mtd/ipc/modules/nfs/grace.ko
	insmod /mnt/mtd/ipc/modules/nfs/lockd.ko
	insmod /mnt/mtd/ipc/modules/nfs/nfs.ko
	insmod /mnt/mtd/ipc/modules/nfs/nfsv2.ko
	insmod /mnt/mtd/ipc/modules/nfs/nfsv3.ko
}

insmod $DRV_PATH/rled.ko
RLEDFLAG=`/mnt/mtd/ipc/readcfg /mnt/mtd/ipc/conf/config_factory.ini common:adctype`
if [ $RLEDFLAG -eq 1 ]
then
	/mnt/mtd/ipc/rledctrl 0
fi

echo "NONE" > /mnt/mtd/ipc/tmpfs/wifi.type
if lsusb | grep "7601" > /dev/null
then
	insmod $DRV_PATH/mtprealloc.ko
	insmod $DRV_PATH/mt7601Usta.ko
	echo "7601" > /mnt/mtd/ipc/tmpfs/wifi.type
fi
if lsusb | grep "f72b" > /dev/null
then
	insmod $DRV_PATH/8733bu.ko
	echo "8731" > /mnt/mtd/ipc/tmpfs/wifi.type
fi
if lsusb | grep "b733" > /dev/null
then
	insmod $DRV_PATH/8733bu.ko
	echo "8731" > /mnt/mtd/ipc/tmpfs/wifi.type
	if [ $WifiType = "Adhoc" ]
	then
		insmod $DRV_PATH/crc16.ko
		insmod $DRV_PATH/firmware_class.ko
		insmod $DRV_PATH/bluetooth.ko
		insmod $DRV_PATH/rtk_btusb.ko
	fi
fi
if lsusb | grep "350b" > /dev/null
then
	cd $DRV_PATH
	insmod $DRV_PATH/ZT9101xV20.ko cfg=/mnt/mtd/ipc/modules/extdrv/wifi.cfg
	echo "9101" > /mnt/mtd/ipc/tmpfs/wifi.type
fi
if lsusb | grep "ffff:3733" > /dev/null
then
	cd $DRV_PATH/ws73
	insmod $DRV_PATH/crc16.ko
	insmod $DRV_PATH/firmware_class.ko
	insmod $DRV_PATH/bluetooth.ko
	insmod $DRV_PATH/ws73/plat_soc.ko
	insmod $DRV_PATH/ws73/wifi_soc.ko
	insmod $DRV_PATH/ws73/ble_soc.ko
	echo "3733" > /mnt/mtd/ipc/tmpfs/wifi.type
fi
insmod $DRV_PATH/extalarm.ko
insmod $DRV_PATH/relay.ko
insmod $DRV_PATH/reset.ko
insmod $DRV_PATH/ircut.ko
insmod $DRV_PATH/trng.ko
insmod $DRV_PATH/wled.ko
insmod $DRV_PATH/rs485.ko
#loadNFS
