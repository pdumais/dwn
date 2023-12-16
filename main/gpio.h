#pragma once

#define PIN_COUNT 43
#define PROV_MODE_NONE 0
#define PROV_MODE_WAP 1
#define PIN_NONE 0
#define PIN_INPUT 1
#define PIN_OUTPUT 2
#define PIN_ANALOG 3
#define PIN_RESERVED 0x80
typedef struct
{
    // This is the time, in seconds, to wait before automatically turn off an output it it stays on for too long
    // It's a kind of fail safe.
    // 0 means "never"
    // 1 is 1 second, so it would effectively make the output act as a pulse
    uint32_t max_on_time[PIN_COUNT];
    __uint8_t pins[PIN_COUNT];
} __attribute__((packed)) pins_config_t;

void dwn_gpio_init();
int get_prog_mode();
void gpio_config_pins();
uint8_t *get_pins_status();
void gpio_set_pin(int pin, int state);
pins_config_t *gpio_get_pins_config();
