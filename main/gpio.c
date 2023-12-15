#include "driver/gpio.h"
#include <esp_system.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_event_base.h>
#include "gpio.h"
#include "common.h"

ESP_EVENT_DEFINE_BASE(GPIO_EVENT);

static const char *TAG = "gpio_c";
static volatile pins_config_t pins_config = {0};
static uint8_t current_status[PIN_COUNT];
static uint32_t current_on[PIN_COUNT];
static uint32_t max_on[PIN_COUNT];

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

pins_config_t *gpio_get_pins_config()
{
	return &pins_config;
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
	if (pin == GPIO_OUTPUT_LED)
	{
		return;
	}

	if (state == 1)
	{
		current_status[pin] = 0;
		current_on[pin] = 0;
		esp_event_post(GPIO_EVENT, GPIO_DOWN, &pin, 4, 0);
	}
	if (state == 0)
	{
		current_status[pin] = 1;
		current_on[pin] = max_on[pin];
		esp_event_post(GPIO_EVENT, GPIO_UP, &pin, 4, 0);
	}
}

void input_pin_check(void *arg)
{
	int level;

	int secslice = 0;
	bool is1sec = false;
	while (1)
	{
		vTaskDelay(PIN_POLL_SLEEP / portTICK_PERIOD_MS);
		secslice += PIN_POLL_SLEEP;
		if (secslice >= 1000)
		{
			is1sec = true;
			secslice = 0;
		}
		else
		{
			is1sec = false;
		}

		for (int i = 0; i < PIN_COUNT; i++)
		{

			if (pins_config.pins[i] == PIN_INPUT)
			{
				level = gpio_get_level(i);
				if (level != current_status[i])
				{
					current_status[i] = level;
					ESP_LOGI(TAG, "Pin changed: %i = %i", i, level);
					if (level == 0)
					{
						esp_event_post(GPIO_EVENT, GPIO_DOWN, &i, 4, 0);
					}
					else
					{
						esp_event_post(GPIO_EVENT, GPIO_UP, &i, 4, 0);
					}
				}
			}
			else if (pins_config.pins[i] == PIN_OUTPUT)
			{
				if (is1sec)
				{
					if (current_on[i])
					{
						current_on[i]--;
						if (current_on[i] == 0)
						{
							gpio_set_pin(i, 1);
						}
					}
				}
			}
		}
	}
}

void gpio_config_pins()
{
	for (int i = 0; i < PIN_COUNT; i++)
	{
		current_status[i] = 0;
		current_on[i] = 0;
	}

	nvs_handle_t nvs_h;

	size_t data_size = sizeof(pins_config);
	ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &nvs_h));

	// If value is not found, we will simply keep the 0-valued structure
	esp_err_t err = nvs_get_blob(nvs_h, "pins", &pins_config, &data_size);
	ESP_LOGI(TAG, "Reading NVS returned %04X", err);
	nvs_close(nvs_h);

	// Sanitize config to configure mandatory pins
	pins_config.pins[GPIO_INPUT_PROV_WAP] = PIN_INPUT;
	pins_config.pins[GPIO_OUTPUT_LED] = PIN_OUTPUT;

	// Valid for ESP32-S3
	pins_config.pins[0] = PIN_RESERVED;
	pins_config.pins[3] = PIN_RESERVED;
	pins_config.pins[8] = PIN_RESERVED;	 // SDA
	pins_config.pins[9] = PIN_RESERVED;	 // SCL
	pins_config.pins[19] = PIN_RESERVED; // uart0
	pins_config.pins[20] = PIN_RESERVED; // uart10
	pins_config.pins[17] = PIN_RESERVED; // uart1
	pins_config.pins[18] = PIN_RESERVED; // uart1
	pins_config.pins[21] = PIN_RESERVED;
	pins_config.pins[22] = PIN_RESERVED;
	pins_config.pins[23] = PIN_RESERVED;
	pins_config.pins[24] = PIN_RESERVED;
	pins_config.pins[25] = PIN_RESERVED;
	pins_config.pins[26] = PIN_RESERVED;
	pins_config.pins[27] = PIN_RESERVED;
	pins_config.pins[28] = PIN_RESERVED;
	pins_config.pins[29] = PIN_RESERVED;
	pins_config.pins[30] = PIN_RESERVED;
	pins_config.pins[31] = PIN_RESERVED;
	pins_config.pins[32] = PIN_RESERVED;
	pins_config.pins[33] = PIN_RESERVED;
	pins_config.pins[34] = PIN_RESERVED;
	pins_config.pins[35] = PIN_RESERVED;
	pins_config.pins[36] = PIN_RESERVED;
	pins_config.pins[37] = PIN_RESERVED;
	pins_config.pins[38] = PIN_RESERVED;

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
			gpio_set_level(i, 1);
			o_conf.pin_bit_mask |= (1ULL << i);
			max_on[i] = pins_config.max_on_time[i];
		}
	}

	i_conf.intr_type = GPIO_INTR_DISABLE;
	i_conf.mode = GPIO_MODE_INPUT;
	i_conf.pull_up_en = 1;
	gpio_config(&i_conf);

	o_conf.intr_type = GPIO_INTR_DISABLE;
	o_conf.mode = GPIO_MODE_OUTPUT;
	o_conf.pull_up_en = 1;
	o_conf.pull_down_en = 0;
	gpio_config(&o_conf);
}

void dwn_gpio_init()
{
	gpio_config_pins();

	xTaskCreate(input_pin_check, "input_check", 4096, NULL, 5, NULL);
}
