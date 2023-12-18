#include "sensors.h"
#include <stdio.h>
#include "math.h"
#include <string.h>
#include <freertos/FreeRTOS.h>

#include <esp_log.h>
#include "bmx280.h"
#include "common.h"

static const char *TAG = "sensor_c";

static bmx280_t *bmx280;
static volatile float temperature = 0;
static volatile float pressure = 0;
static volatile float humidity = 0;

float get_temperature()
{
    if (!bmx280)
    {
        return -1;
    }
    return temperature;
}

float get_pressure()
{
    if (!bmx280)
    {
        return -1;
    }
    return pressure;
}

float get_humidity()
{
    if (!bmx280)
    {
        return -1;
    }
    if (!bmx280_isBME(bmx280->chip_id))
    {
        return -1;
    }
    return humidity;
}

void bme280_check(void *arg)
{
    while (1)
    {
        vTaskDelay(BME_POLL_SLEEP / portTICK_PERIOD_MS);
        ESP_ERROR_CHECK(bmx280_setMode(bmx280, BMX280_MODE_FORCE));
        while (bmx280_isSampling(bmx280))
        {
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
        float temp = 0, pres = 0, hum = 0;
        ESP_ERROR_CHECK(bmx280_readoutFloat(bmx280, &temp, &pres, &hum));
        temperature = temp;
        pressure = pres;
        humidity = hum;

        // ESP_LOGI("test", "Read Values: temp = %0.1f, pres = %0.1f, hum = %f", temperature, pressure, humidity);
    }
}

void init_sensors()
{
    bmx280 = bmx280_create(I2C_NUM_0);

    esp_err_t err = bmx280_init(bmx280);
    if (err != ESP_OK)
    {
        bmx280 = 0;
        ESP_LOGI(TAG, "BME280 not found");
    }
    else
    {

        bmx280_config_t bmx_cfg = BMX280_DEFAULT_CONFIG;
        ESP_ERROR_CHECK(bmx280_configure(bmx280, &bmx_cfg));

        xTaskCreate(bme280_check, "bme280_check", 4096, NULL, 5, NULL);
    }
}