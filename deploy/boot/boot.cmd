# i.MX95 EVK U-Boot Boot Script
# Implements A/B slot selection with fallback

# Determine active slot
setexpr slot_a 'a'
setexpr slot_b 'b'

if test "${boot_slot}" = "a"; then
    setenv boot_part ${boot_a_part}
elif test "${boot_slot}" = "b"; then
    setenv boot_part ${boot_b_part}
else
    setenv boot_slot a
    setenv boot_part ${boot_a_part}
fi

# Decrement boot_attempt
if test ${boot_attempt} -gt 0; then
    setexpr boot_attempt ${boot_attempt} - 1
    saveenv
else
    # Fallback to alternate slot
    if test "${boot_slot}" = "a"; then
        setenv boot_slot b
        setenv boot_part ${boot_b_part}
    else
        setenv boot_slot a
        setenv boot_part ${boot_a_part}
    fi
    setenv boot_attempt 3
    saveenv
fi

# Load kernel and device tree
fatload mmc ${mmcdev}:${boot_part} ${loadaddr} Image
fatload mmc ${mmcdev}:${boot_part} ${fdt_addr} ${fdt_file}
fatload mmc ${mmcdev}:${boot_part} ${ramdisk_addr} initramfs.cpio.gz

# Boot arguments
setenv bootargs console=${console},${baudrate} root=/dev/mmcblk${mmcdev}p${rootfs_part} rw rootwait

# Boot
booti ${loadaddr} ${ramdisk_addr} ${fdt_addr}
