#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>

#include <esp_log.h>
#include "esp_chip_info.h"
#include <esp_wifi.h>
#include "esp_flash.h"
#include <esp_system.h>
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "http_server.h"
#include "common.h"
#include "api.h"
#include "gpio.h"

// idf.py add-dependency "espressif/esp_websocket_client^1.0.0"

static const char *TAG = "api_c";

// curl http://192.168.1.46:/jsonrpc -d '{"jsonrpc": "2.0", "method": "configpins", "params": {"outputs":[13,14], "inputs":[22,23]}, "id": "1"}'
static esp_err_t post_pins_handler(cJSON *params)
{
    const cJSON *pin;
    pins_config_t pins_config = {PIN_NONE};

    cJSON_ArrayForEach(pin, cJSON_GetObjectItemCaseSensitive(params, "inputs"))
    {
        int pin_number = pin->valueint;
        if (pin_number >= 0 && pin_number < sizeof(pins_config.pins))
        {
            pins_config.pins[pin_number] = PIN_INPUT;
        }
    }
    cJSON_ArrayForEach(pin, cJSON_GetObjectItemCaseSensitive(params, "outputs"))
    {
        int pin_number = pin->valueint;
        if (pin_number >= 0 && pin_number < sizeof(pins_config.pins))
        {
            pins_config.pins[pin_number] = PIN_OUTPUT;
        }
    }

    // Store config in NVS
    nvs_handle_t nvs_h;
    size_t data_size = sizeof(pins_config);
    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &nvs_h));
    nvs_set_blob(nvs_h, "pins", &pins_config, data_size);
    ESP_ERROR_CHECK(nvs_commit(nvs_h));
    nvs_close(nvs_h);

    gpio_config_pins();
    return ESP_OK;
}


static esp_err_t set_pin_handler(cJSON *params)
{
    int pin_num = cJSON_GetObjectItem(params, "pin")->valueint;
    gpio_set_pin(pin_num, 0);
    return ESP_OK;
}

static esp_err_t clear_pin_handler(cJSON *params)
{
    int pin_num = cJSON_GetObjectItem(params, "pin")->valueint;
    gpio_set_pin(pin_num, 1);
    return ESP_OK;
}

// curl http://192.168.1.46:/jsonrpc -d '{"jsonrpc": "2.0", "method": "getpins", "params": {}, "id": "1"}'
static void get_pins_handler(cJSON *ret)
{
    cJSON *arr = cJSON_CreateArray();
    uint8_t *pins = get_pins_status();
    for (int i = 0; i < PIN_COUNT; i++)
    {
        if (pins[i] != 0)
        {
            cJSON *p = cJSON_CreateNumber(i);
            cJSON_AddItemToArray(arr, p);
        }
    }
    cJSON_AddItemToObject(ret, "pins", arr);
}

// curl http://192.168.1.46:/jsonrpc -d '{"jsonrpc": "2.0", "method": "restart", "params": {"delete": true/false}, "id": "1"}'
static void restart_handler(cJSON *params)
{
    cJSON *del = cJSON_GetObjectItem(params, "delete");
    if (cJSON_IsTrue(del)) // this will also check for null
    {
        wifi_config_t current_conf;
        memset(current_conf.sta.ssid, 0, sizeof(current_conf.sta.ssid));
        memset(current_conf.sta.password, 0, sizeof(current_conf.sta.password));
        esp_wifi_set_config((wifi_interface_t)ESP_IF_WIFI_STA, &current_conf);
    }

    esp_restart();
}

// curl http://192.168.1.46:/jsonrpc -d '{"jsonrpc": "2.0", "method": "status", "params": {}, "id": "1"}'
static esp_err_t get_status_handler(cJSON *ret)
{
    cJSON *arr = cJSON_CreateArray();
    uint8_t *pins = get_pins_status();
    for (int i = 0; i < PIN_COUNT; i++)
    {
        if (pins[i] != 0)
        {
            cJSON *p = cJSON_CreateNumber(i);
            cJSON_AddItemToArray(arr, p);
        }
    }

    cJSON_AddItemToObject(ret, "device", cJSON_CreateString("DWN"));

    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);

    cJSON_AddItemToObject(ret, "cores", cJSON_CreateNumber(chip_info.cores));
    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    cJSON_AddItemToObject(ret, "silicon_revision_major", cJSON_CreateNumber(major_rev));
    cJSON_AddItemToObject(ret, "silicon_revision_minor", cJSON_CreateNumber(minor_rev));

    esp_flash_get_size(NULL, &flash_size);
    cJSON_AddItemToObject(ret, "flash_size", cJSON_CreateNumber(flash_size / (uint32_t)(1024 * 1024)));
    cJSON_AddItemToObject(ret, "free_heap", cJSON_CreateNumber(esp_get_minimum_free_heap_size()));

    char str[32];
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(str, 20, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
    cJSON_AddItemToObject(ret, "mac", cJSON_CreateString(str));

    return ESP_OK;
}

int jsonrpc_handler(const char *method, cJSON *params, cJSON *ret)
{
    #define RUN_HANDLER(_m, _h) if (!strcmp(method, _m)) {_h; return 0;}

    RUN_HANDLER("restart", restart_handler(params))
    RUN_HANDLER("getpins", get_pins_handler(ret))
    RUN_HANDLER("setpin", set_pin_handler(params))
    RUN_HANDLER("clearpin", clear_pin_handler(params))
    RUN_HANDLER("status", get_status_handler(ret))
    RUN_HANDLER("configpins", post_pins_handler(params))

    return -1;
}



void start_api()
{
    start_webserver();
    register_jsonrpc_handler(jsonrpc_handler);
}