#include "bb_hal_pwm.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define SYSFS_PWM "/sys/class/pwm"

int bb_pwm_open(bb_pwm_t *pwm, int chip, int channel) {
    memset(pwm, 0, sizeof(*pwm));
    pwm->chip = chip;
    pwm->channel = channel;

    char path[128];
    snprintf(path, sizeof(path), SYSFS_PWM "/pwmchip%d/export", chip);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%d", channel);
    fclose(f);

    usleep(100000);

    snprintf(path, sizeof(path), SYSFS_PWM "/pwmchip%d/pwm%d/period", chip, channel);
    if (access(path, W_OK) != 0) return -1;

    pwm->exported = 1;
    return 0;
}

static int pwm_write_attr(bb_pwm_t *pwm, const char *attr, const char *value) {
    char path[128];
    snprintf(path, sizeof(path), SYSFS_PWM "/pwmchip%d/pwm%d/%s",
             pwm->chip, pwm->channel, attr);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%s", value);
    fclose(f);
    return 0;
}

int bb_pwm_set_period(bb_pwm_t *pwm, uint32_t period_ns) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", period_ns);
    return pwm_write_attr(pwm, "period", buf);
}

int bb_pwm_set_duty(bb_pwm_t *pwm, uint32_t duty_ns) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", duty_ns);
    return pwm_write_attr(pwm, "duty_cycle", buf);
}

int bb_pwm_enable(bb_pwm_t *pwm) {
    return pwm_write_attr(pwm, "enable", "1");
}

int bb_pwm_disable(bb_pwm_t *pwm) {
    return pwm_write_attr(pwm, "enable", "0");
}

void bb_pwm_close(bb_pwm_t *pwm) {
    if (pwm->exported) {
        char path[128];
        snprintf(path, sizeof(path), SYSFS_PWM "/pwmchip%d/unexport", pwm->chip);
        FILE *f = fopen(path, "w");
        if (f) { fprintf(f, "%d", pwm->channel); fclose(f); }
        pwm->exported = 0;
    }
}
