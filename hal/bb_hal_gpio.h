#ifndef BB_HAL_GPIO_H
#define BB_HAL_GPIO_H

typedef enum {
    BB_GPIO_IN  = 0,
    BB_GPIO_OUT = 1,
} bb_gpio_direction_t;

typedef enum {
    BB_GPIO_EDGE_NONE    = 0,
    BB_GPIO_EDGE_RISING  = 1,
    BB_GPIO_EDGE_FALLING = 2,
    BB_GPIO_EDGE_BOTH    = 3,
} bb_gpio_edge_t;

typedef struct {
    int  num;
    int  chip_fd;
    int  line_fd;
    int  line_offset;
    int  is_sysfs;
    int  exported;
} bb_gpio_t;

int  bb_gpio_open(bb_gpio_t *gpio, int num, bb_gpio_direction_t dir);
int  bb_gpio_set_direction(bb_gpio_t *gpio, bb_gpio_direction_t dir);
int  bb_gpio_read(bb_gpio_t *gpio);
int  bb_gpio_write(bb_gpio_t *gpio, int value);
int  bb_gpio_set_edge(bb_gpio_t *gpio, bb_gpio_edge_t edge);
int  bb_gpio_poll(bb_gpio_t *gpio, int timeout_ms);
void bb_gpio_close(bb_gpio_t *gpio);

#endif
