#ifndef BB_HAL_PWM_H
#define BB_HAL_PWM_H
#include <stdint.h>

typedef struct {
    int  chip;
    int  channel;
    int  exported;
} bb_pwm_t;

int  bb_pwm_open(bb_pwm_t *pwm, int chip, int channel);
int  bb_pwm_set_period(bb_pwm_t *pwm, uint32_t period_ns);
int  bb_pwm_set_duty(bb_pwm_t *pwm, uint32_t duty_ns);
int  bb_pwm_enable(bb_pwm_t *pwm);
int  bb_pwm_disable(bb_pwm_t *pwm);
void bb_pwm_close(bb_pwm_t *pwm);

#endif
