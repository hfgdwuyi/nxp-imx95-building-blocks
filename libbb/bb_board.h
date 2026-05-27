#ifndef BB_BOARD_H
#define BB_BOARD_H

/*
 * Board abstraction layer for NXP i.MX95 EVK.
 *
 * The i.MX95 features:
 *   - 6x Cortex-A55 cores
 *   - 1x Cortex-M7 (real-time domain)
 *   - 1x Cortex-M33 (safety domain)
 *   - eIQ Neutron NPU
 *   - ISP, GPU, VPU
 *
 * Build: make                   # native on i.MX95 board
 *        make cross             # aarch64-linux-gnu-gcc
 *        make cross-evk         # with BOARD_NXP_IMX95_EVK
 */

#if !defined(BOARD_NXP_IMX95_EVK)
#  warning "No board defined — defaulting to BOARD_NXP_IMX95_EVK"
#  define BOARD_NXP_IMX95_EVK
#endif

#ifdef BOARD_NXP_IMX95_EVK

#  define BB_PRODUCT_NAME      "NXP i.MX95 EVK"
#  define BB_DTB_FILE          "imx95-evk.dtb"
#  define BB_CONSOLE_DEV       "/dev/ttyLP0"
#  define BB_CONSOLE_BAUD      115200

// LEDs — i.MX95 EVK has "green:user" LED
#  define BB_LED_HEARTBEAT     "green:user"
#  define BB_LED1              "green:user"
#  define BB_LED2              ""

// SPI — i.MX95 LPSPI
#  define BB_SPI_DEV           "/dev/spidev0.0"

// I2C buses
#  define BB_I2C_COUNT         3
#  define BB_I2C_BUSES         {0, 1, 2}
#  define BB_I2C_LABELS        {"i2c-0 (PMIC)","i2c-1","i2c-2"}

// GPIO
#  define BB_GPIO_TEST_PIN     13

// UART — i.MX95 uses ttyLP (LPUART) instead of ttymxc
#  define BB_UART_COUNT        4
#  define BB_UART_DEVS         {"/dev/ttyLP0","/dev/ttyLP1","/dev/ttyLP2","/dev/ttyLP3"}
#  define BB_UART_LABELS       {"ttyLP0 (console)","ttyLP1","ttyLP2","ttyLP3"}

// PWM
#  define BB_PWM_TEST_CHIPS    {0, 1}
#  define BB_PWM_TEST_COUNT    2

// RTC — i.MX95 has SNVS RTC
#  define BB_RTC_DEV           "/dev/rtc0"

// Watchdog
#  define BB_WDG_DEV           "/dev/watchdog0"

// Root device (eMMC: mmcblk0, SD: mmcblk1)
#  define BB_ROOT_DEV          "/dev/mmcblk0"

// Partition layout (A/B scheme)
#  define BB_PART_BOOT_A       4
#  define BB_PART_BOOT_B       5
#  define BB_PART_ROOTFS_A     6
#  define BB_PART_ROOTFS_B     7
#  define BB_PART_RECOVERY     8
#  define BB_PART_PERSIST      9
#  define BB_PART_MFG          10
#  define BB_PART_LOG          11

// Network deploy target
#  define BB_DEPLOY_HOST       "192.168.0.232"
#  define BB_DEPLOY_USER       "root"
#  define BB_INSTALL_PREFIX    "/opt/building-blocks"

#endif // BOARD_NXP_IMX95_EVK

#endif // BB_BOARD_H
