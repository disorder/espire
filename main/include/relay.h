#ifndef __RELAY_H__
#define __RELAY_H__

void relay_init(int count);
void relay_init_pin(int pin);
void relay_init_gpio(int gpio);
void relay_set_pin(int pin, int state);
void relay_set_gpio(int gpio, int state);

#endif /* __RELAY_H__ */
