#ifndef BB_HAL_LED_H
#define BB_HAL_LED_H

typedef struct {
    char name[64];
    char brightness_path[128];
    char trigger_path[128];
    int  max_brightness;
} bb_led_t;

int bb_led_open(bb_led_t *led, const char *name);
int bb_led_set_brightness(bb_led_t *led, int value);
int bb_led_set_trigger(bb_led_t *led, const char *trigger);
int bb_led_on(bb_led_t *led);
int bb_led_off(bb_led_t *led);

#endif
