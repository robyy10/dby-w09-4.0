### AnyKernel3 Ramdisk Mod Script
## DBY-W09 (Huawei MatePad 11 2021 / Snapdragon 865)

### AnyKernel setup
properties() { '
kernel.string=DBY-W09 Kernel 4.19.157 @ HarmonyOS 3.0
do.devicecheck=1
do.modules=0
do.systemless=1
do.cleanup=1
do.cleanuponabort=0
device.name1=DBY-W09
device.name2=HWDBY-W09
device.name3=DBY_W09
device.name4=
device.name5=
supported.versions=
supported.patchlevels=
supported.vendorpatchlevels=
'; } # end properties

### AnyKernel install
## boot files attributes
boot_attributes() {
set_perm_recursive 0 0 755 644 $RAMDISK/*;
set_perm_recursive 0 0 750 750 $RAMDISK/init* $RAMDISK/sbin;
} # end attributes

# boot shell variables
# Snapdragon 865 (Kona) - A/B partitioned device
BLOCK=/dev/block/bootdevice/by-name/boot;
IS_SLOT_DEVICE=1;
RAMDISK_COMPRESSION=auto;
PATCH_VBMETA_FLAG=auto;

# import functions/variables and setup patching
. tools/ak3-core.sh;

# boot install
dump_boot;
write_boot;
