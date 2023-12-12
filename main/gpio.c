#include "driver/gpio.h"
#include <esp_system.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include "gpio.h"
#include "common.h"

static const char *TAG = "gpio_c";
static pins_config_t pins_config = {0};
static uint8_t current_status[PIN_COUNT];

int get_prog_mode()
{
	int prov_wap = gpio_get_level(GPIO_INPUT_PROV_WAP);
	ESP_LOGI(TAG, "Prog mode: %i", prov_wap);
	return (!prov_wap);
}

uint8_t *get_pins_status()
{
	return &current_status;
}

void gpio_set_pin(int pin, int state)
{
	if (pin < 0)
		return;
	if (pin >= PIN_COUNT)
		return;
	if (pins_config.pins[pin] != PIN_OUTPUT)
		return;

	gpio_set_level(pin, state);
}

void input_pin_check(void *arg)
{
	int level;

	for (int i = 0; i < PIN_COUNT; i++)
	{
		current_status[i] = 0;
	}

	while (1)
	{
		vTaskDelay(PIN_POLL_SLEEP / portTICK_PERIOD_MS);
		for (int i = 0; i < PIN_COUNT; i++)
		{
			if (pins_config.pins[i] == PIN_INPUT)
			{
				level = gpio_get_level(i);
				if (level != current_status[i])
				{
					current_status[i] = level;
					ESP_LOGI(TAG, "Pin changed: %i = %i", i, level);
				}
			}
		}
	}
}

void gpio_config_pins()
{
	nvs_handle_t nvs_h;

	size_t data_size = PIN_COUNT;
	ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &nvs_h));

	// If value is not found, we will simply keep the 0-valued structure
	nvs_get_blob(nvs_h, "pins", &pins_config, &data_size);
	nvs_close(nvs_h);

	// Sanitize config to configure mandatory pins
	// TODO: LED
	pins_config.pins[GPIO_INPUT_PROV_WAP] = PIN_INPUT;
	pins_config.pins[GPIO_OUTPUT_LED] = PIN_OUTPUT;

	gpio_config_t i_conf = {};
	gpio_config_t o_conf = {};
	i_conf.pin_bit_mask = 0;
	o_conf.pin_bit_mask = 0;
	for (int i = 0; i < PIN_COUNT; i++)
	{
		if (pins_config.pins[i] == PIN_INPUT)
		{
			i_conf.pin_bit_mask |= (1ULL << i);
		}
		if (pins_config.pins[i] == PIN_OUTPUT)
		{
			o_conf.pin_bit_mask |= (1ULL << i);
		}
	}

	i_conf.intr_type = GPIO_INTR_DISABLE;
	i_conf.mode = GPIO_MODE_INPUT;
	i_conf.pull_up_en = 1;
	gpio_config(&i_conf);

	o_conf.intr_type = GPIO_INTR_DISABLE;
	o_conf.mode = GPIO_MODE_OUTPUT;
	o_conf.pull_up_en = 0;
	o_conf.pull_down_en = 1;
	gpio_config(&o_conf);
}

void dwn_gpio_init()
{
	gpio_config_pins();

	xTaskCreate(input_pin_check, "input_check", 4096, NULL, 5, NULL);
}
