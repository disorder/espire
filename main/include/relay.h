#ifndef __RELAY_H__
#define __RELAY_H__

void relay_init(int count, int v5);
void relay_init_pin(int pin, int v5);
void relay_init_gpio(int gpio, int v5);
void relay_set_pin(int pin, int state);
void relay_set_pin_5v(int pin, int state);
void relay_set_gpio(int gpio, int state);
void relay_set_gpio_5v(int gpio, int state);
void relay_reset_gpio(int gpio);

#endif /* __RELAY_H__ */
