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
#include "ota.h"

#include "http_server.h"
#include "common.h"
#include "api.h"
#include "gpio.h"

// idf.py add-dependency "espressif/esp_websocket_client^1.0.0"

static const char *TAG = "api_c";

// curl http://192.168.1.46:/jsonrpc -d '{"jsonrpc": "2.0", "method": "configpins", "params": {"pins":[{"pin":16, "mode":1, "maxon":0}, {"pin":14, "mode":2, "maxon":0}]}, "id": "1"}'
static esp_err_t post_pins_handler(cJSON *params)
{
    const cJSON *pin;
    pins_config_t *pins_config = gpio_get_pins_config();

    cJSON_ArrayForEach(pin, cJSON_GetObjectItemCaseSensitive(params, "pins"))
    {
        int pin_number = cJSON_GetObjectItemCaseSensitive(pin, "pin")->valueint;
        if (pin_number < 0 || pin_number >= PIN_COUNT)
        {
            return ESP_FAIL;
        }

        int mode = cJSON_GetObjectItemCaseSensitive(pin, "mode")->valueint;
        int maxon = 0;
        if (mode == PIN_OUTPUT)
        {
            maxon = cJSON_GetObjectItemCaseSensitive(pin, "maxon")->valueint;
        }
        pins_config->pins[pin_number] = mode;
        pins_config->max_on_time[pin_number] = maxon;
    }

    // Store config in NVS
    nvs_handle_t nvs_h;
    size_t data_size = sizeof(pins_config);
    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &nvs_h));
    esp_err_t err = nvs_set_blob(nvs_h, "pins", pins_config, data_size);
    ESP_LOGI(TAG, "Writing to NVS returned %04X", err);
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
            // TODO: must return whole list with their analog/digital value
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

// curl http://192.168.1.46:/jsonrpc -d '{"jsonrpc": "2.0", "method": "ota", "params": {"upgrade_url": "http://192.168.1.3:8113/dwn.bin"}, "id": "1"}'
static void ota_handler(cJSON *params)
{
    cJSON *server = cJSON_GetObjectItem(params, "upgrade_url");
    start_ota_upgrade(server->valuestring);
}

// curl http://192.168.1.46:/jsonrpc -d '{"jsonrpc": "2.0", "method": "status", "params": {}, "id": "1"}'
static esp_err_t get_status_handler(cJSON *ret)
{
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

    nvs_handle_t nvs_h;
    pins_config_t *pins_config = gpio_get_pins_config();

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < PIN_COUNT; i++)
    {
        cJSON *pobj = cJSON_CreateObject();
        cJSON_AddItemToObject(pobj, "mode", cJSON_CreateNumber(pins_config->pins[i]));
        cJSON_AddItemToObject(pobj, "maxon", cJSON_CreateNumber(pins_config->max_on_time[i]));
        cJSON_AddItemToArray(arr, pobj);
    }
    cJSON_AddItemToObject(ret, "pins", arr);

    return ESP_OK;
}

int jsonrpc_handler(const char *method, cJSON *params, cJSON *ret)
{
#define RUN_HANDLER(_m, _h)  \
    if (!strcmp(method, _m)) \
    {                        \
        _h;                  \
        return 0;            \
    }

    RUN_HANDLER("restart", restart_handler(params))
    RUN_HANDLER("getpins", get_pins_handler(ret))
    RUN_HANDLER("setpin", set_pin_handler(params))
    RUN_HANDLER("clearpin", clear_pin_handler(params))
    RUN_HANDLER("status", get_status_handler(ret))
    RUN_HANDLER("configpins", post_pins_handler(params))
    RUN_HANDLER("ota", ota_handler(params))

    return -1;
}

void on_gpio_high(void *arg, esp_event_base_t event_base, int event_id, void *event_data)
{
    int pin = ((int *)event_data)[0];
    ESP_LOGI(TAG, "Pin High %i", pin);

    // When pin is high (3.3v), it is not being asserted
    char str[256];
    sprintf(str, "{\"event\":\"pin\", \"state\":false, \"pin\": %i}", pin);
    broadcast_ws(str);
}

void on_gpio_low(void *arg, esp_event_base_t event_base, int event_id, void *event_data)
{
    int pin = ((int *)event_data)[0];
    ESP_LOGI(TAG, "Pin Low %i", pin);

    // When pin is low (0v), it is being asserted to ground
    char str[256];
    sprintf(str, "{\"event\":\"pin\", \"state\":true, \"pin\": %i}", pin);
    broadcast_ws(str);
}

static esp_err_t html_handler(httpd_req_t *req)
{
    extern const unsigned char html_start[] asm("_binary_index_html_start");
    extern const unsigned char html_end[] asm("_binary_index_html_end");
    const size_t html_size = (html_end - html_start);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)html_start, html_size);
    return ESP_OK;
}

void start_api()
{
    start_webserver();
    init_ws();
    register_jsonrpc_handler(jsonrpc_handler);
    register_uri("/", HTTP_GET, html_handler);

    ESP_ERROR_CHECK(esp_event_handler_register(GPIO_EVENT, GPIO_UP, &on_gpio_high, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(GPIO_EVENT, GPIO_DOWN, &on_gpio_low, NULL));

    /*while (1)
    {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        broadcast_ws("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}");
    }*/
}