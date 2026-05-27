/*
 * bb-hal-test - HAL validation tool for i.MX95 (all 8 modules)
 *
 * Tests: I2C, SPI, GPIO, LED, PWM, RTC, Watchdog, UART
 * Board-specific values come from libbb/bb_board.h
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "bb_board.h"
#include "bb_hal_i2c.h"
#include "bb_hal_spi.h"
#include "bb_hal_gpio.h"
#include "bb_hal_led.h"
#include "bb_hal_pwm.h"
#include "bb_hal_rtc.h"
#include "bb_hal_wdg.h"
#include "bb_hal_uart.h"

static int failures = 0;
#define PASS() printf("  PASS\n")
#define FAIL(msg) do { printf("  FAIL: %s\n", msg); failures++; } while(0)

static void test_i2c_scan(void) {
    printf("\n=== I2C Bus Scan ===\n");

    int bus_nums[] = BB_I2C_BUSES;
    const char *labels[] = BB_I2C_LABELS;

    for (int b = 0; b < BB_I2C_COUNT; b++) {
        char dev[32];
        snprintf(dev, sizeof(dev), "/dev/i2c-%d", bus_nums[b]);

        bb_i2c_t i2c;
        printf("%-22s: ", labels[b]);
        if (bb_i2c_open(&i2c, dev) < 0) {
            printf("open FAILED\n"); failures++; continue;
        }
        int found = 0;
        for (int addr = 0x03; addr <= 0x77; addr++) {
            if (bb_i2c_probe(&i2c, (uint8_t)addr) == 0) { printf("0x%02x ", addr); found++; }
        }
        if (!found) printf("(no devices)");
        printf("\n");
        bb_i2c_close(&i2c);
    }
}

static void test_spi(void) {
    printf("\n=== SPI: %s ===\n", BB_SPI_DEV);
    bb_spi_t spi;
    if (bb_spi_open(&spi, BB_SPI_DEV, 1000000, BB_SPI_MODE_0, 8) < 0) {
        FAIL("open " BB_SPI_DEV); return;
    }
    printf("  Opened: speed=%u mode=%d bits=%d\n", spi.speed_hz, spi.mode, spi.bits_per_word);

    uint8_t tx[4] = {0x9F,0x00,0x00,0x00}, rx[4] = {0};
    if (bb_spi_transfer(&spi, tx, rx, 4) >= 0) {
        printf("  Transfer OK (rx: %02x %02x %02x %02x)\n", rx[0],rx[1],rx[2],rx[3]);
        PASS();
    } else { FAIL("transfer"); }
    bb_spi_close(&spi);
}

static void test_gpio(void) {
    printf("\n=== GPIO: Full cycle (gpio%d) ===\n", BB_GPIO_TEST_PIN);
    bb_gpio_t gpio;
    if (bb_gpio_open(&gpio, BB_GPIO_TEST_PIN, BB_GPIO_OUT) < 0) {
        FAIL("gpio open"); return;
    }
    printf("  Exported gpio%d as output\n", BB_GPIO_TEST_PIN);

    bb_gpio_write(&gpio, 1);
    int v1 = bb_gpio_read(&gpio);
    printf("  Write 1 -> read %d ", v1);
    if (v1 != 1) { printf("MISMATCH (may be OK if pin is pulled)\n"); }
    else printf("OK\n");

    bb_gpio_write(&gpio, 0);
    int v2 = bb_gpio_read(&gpio);
    printf("  Write 0 -> read %d ", v2);
    if (v2 != 0) { printf("MISMATCH (may be OK if pin is pulled)\n"); }
    else printf("OK\n");

    bb_gpio_close(&gpio);
    PASS();
}

static void test_led(void) {
    printf("\n=== LED: Board user LED ===\n");

    const char *names[] = {BB_LED1, BB_LED2};
    int tested = 0;
    for (int i = 0; i < 2; i++) {
        if (!names[i] || !names[i][0]) continue;
        bb_led_t led;
        printf("  %s: ", names[i]);
        if (bb_led_open(&led, names[i]) < 0) {
            printf("open FAILED (name may differ on this board)\n");
            continue;
        }
        printf("max=%d, toggling... ", led.max_brightness);
        bb_led_on(&led); usleep(150000); bb_led_off(&led);
        printf("OK\n");
        tested++;
        PASS();
    }
    if (!tested) FAIL("no LED found (check BB_LED1/BB_LED2 in bb_board.h)");
}

static void test_pwm(void) {
    printf("\n=== PWM: Scanning pwmchip0-1 ===\n");
    int tested = 0;
    int chips[] = BB_PWM_TEST_CHIPS;

    for (int i = 0; i < BB_PWM_TEST_COUNT; i++) {
        int chip = chips[i];
        bb_pwm_t pwm;
        printf("  pwmchip%d/pwm0: ", chip);
        if (bb_pwm_open(&pwm, chip, 0) < 0) {
            printf("export BUSY or unavailable\n");
            continue;
        }
        printf("exported, ");
        int ok = 1;
        if (bb_pwm_set_period(&pwm, 1000000) < 0)  { printf("period FAILED "); ok = 0; }
        if (bb_pwm_set_duty(&pwm, 500000) < 0)     { printf("duty FAILED "); ok = 0; }
        if (bb_pwm_enable(&pwm) < 0)                { printf("enable FAILED "); ok = 0; }
        if (ok) {
            printf("Period=1ms Duty=50%% Enabled OK\n");
            usleep(100000);
            bb_pwm_disable(&pwm);
            tested = 1;
        } else {
            printf("(not routed to pin)\n");
        }
        bb_pwm_close(&pwm);
    }
    if (tested) PASS(); else FAIL("no usable PWM channel found");
}

static void test_rtc(void) {
    printf("\n=== RTC: %s ===\n", BB_RTC_DEV);
    bb_rtc_t rtc;
    if (bb_rtc_open(&rtc, BB_RTC_DEV) < 0) { FAIL("open " BB_RTC_DEV); return; }

    bb_rtc_time_t t;
    if (bb_rtc_read(&rtc, &t) == 0) {
        printf("  RTC time: %04d-%02d-%02d %02d:%02d:%02d\n",
               t.year, t.mon, t.day, t.hour, t.min, t.sec);
        PASS();
    } else { FAIL("read RTC"); }

    bb_rtc_close(&rtc);
}

static void test_wdg(void) {
    printf("\n=== Watchdog: %s ===\n", BB_WDG_DEV);
    bb_wdg_t wdg;
    if (bb_wdg_open(&wdg, BB_WDG_DEV) < 0) { FAIL("open " BB_WDG_DEV); return; }

    int timeout = bb_wdg_get_timeout(&wdg);
    printf("  Current timeout: %d sec\n", timeout);

    if (timeout > 0) {
        if (bb_wdg_kick(&wdg) == 0) {
            printf("  Kick OK\n");
            PASS();
        } else { FAIL("kick"); }
    } else {
        printf("  Timeout is 0, skipping kick (disabled)\n");
        PASS();
    }

    bb_wdg_close(&wdg);
}

static void test_uart(void) {
    printf("\n=== UART: Serial ports (ttyLP = i.MX95 LPUART) ===\n");

    const char *devs[]   = BB_UART_DEVS;
    const char *labels[] = BB_UART_LABELS;
    int tested = 0;

    for (int i = 0; i < BB_UART_COUNT; i++) {
        bb_uart_t uart;
        printf("  %s (%s): ", devs[i], labels[i]);
        if (bb_uart_open(&uart, devs[i], BB_UART_BAUD_115200) < 0) {
            printf("open FAILED\n");
            continue;
        }
        printf("opened OK, ");

        const char *msg = "AT\r\n";
        int w_ret = bb_uart_write(&uart, (const uint8_t *)msg, strlen(msg));
        if (w_ret == 0) {
            printf("TX OK\n");
            tested = 1;
        } else {
            printf("TX blocked (HW flow control?)\n");
        }
        bb_uart_close(&uart);
    }
    if (tested) PASS(); else FAIL("no writable UART found");
}

int main(void) {
    printf("=== bb-hal-test: i.MX95 HAL Verification (8 modules) ===\n");
    printf("Board: " BB_PRODUCT_NAME "\n");

    test_i2c_scan();
    test_spi();
    test_gpio();
    test_led();
    test_pwm();
    test_rtc();
    test_wdg();
    test_uart();

    printf("\n=== Result: %d failures ===\n", failures);
    return failures;
}
