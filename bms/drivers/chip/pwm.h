#include <stdbool.h>
#include <stdint.h>

void init_pwm_pin(unsigned int pin, bool invert_b);
void pwm_set(unsigned int pin, uint32_t level, bool invert_b);
