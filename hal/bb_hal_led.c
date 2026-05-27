#include "bb_hal_led.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int bb_led_open(bb_led_t *led, const char *name) {
    memset(led, 0, sizeof(*led));
    strncpy(led->name, name, sizeof(led->name) - 1);

    snprintf(led->brightness_path, sizeof(led->brightness_path),
             "/sys/class/leds/%s/brightness", name);
    snprintf(led->trigger_path, sizeof(led->trigger_path),
             "/sys/class/leds/%s/trigger", name);

    char max_path[128];
    snprintf(max_path, sizeof(max_path), "/sys/class/leds/%s/max_brightness", name);
    FILE *f = fopen(max_path, "r");
    if (f) {
        if (fscanf(f, "%d", &led->max_brightness) != 1) {
            led->max_brightness = 1;
        }
        fclose(f);
    } else {
        led->max_brightness = 1;
    }

    if (access(led->brightness_path, W_OK) != 0) {
        return -1;
    }
    return 0;
}

int bb_led_set_brightness(bb_led_t *led, int value) {
    FILE *f = fopen(led->brightness_path, "w");
    if (!f) return -1;
    fprintf(f, "%d", value);
    fclose(f);
    return 0;
}

int bb_led_set_trigger(bb_led_t *led, const char *trigger) {
    FILE *f = fopen(led->trigger_path, "w");
    if (!f) return -1;
    fprintf(f, "%s", trigger);
    fclose(f);
    return 0;
}

int bb_led_on(bb_led_t *led) {
    return bb_led_set_brightness(led, led->max_brightness);
}

int bb_led_off(bb_led_t *led) {
    return bb_led_set_brightness(led, 0);
}
