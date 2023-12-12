#pragma once

#define PIN_COUNT 43
#define PROV_MODE_NONE 0
#define PROV_MODE_WAP 1
#define PIN_NONE 0
#define PIN_INPUT 1
#define PIN_OUTPUT 2

void dwn_gpio_init();
int get_prog_mode();
void gpio_config_pins();
uint8_t *get_pins_status();
void gpio_set_pin(int pin, int state);

typedef struct
{
    __uint8_t pins[PIN_COUNT];
} __attribute__((packed)) pins_config_t;