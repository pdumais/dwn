#include <string.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_event.h>
#include <freertos/event_groups.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "nvs_flash.h"
#include "nvs.h"
#include <netdb.h>
#include "gpio.h"
#include "provision.h"

#define WIFI_STATE_NONE 0
#define WIFI_STATE_AP 1
#define WIFI_STATE_CONNECTING 2
#define WIFI_STATE_CONNECTED 3

static const char *TAG = "wifi_c";
static esp_netif_t *sta_netif;
static esp_netif_t *ap_netif;
static volatile int wifi_state;

/* Signal Wi-Fi events on this event-group */
const int WIFI_CONNECTED_EVENT = BIT0;
static EventGroupHandle_t wifi_event_group;

void wifi_led(void *arg)
{
    int seq_connecting[] = {500, 500, 0};
    int seq_connected[] = {50, 50, 50, 850, 0};
    int seq_ap[] = {800, 200, 0};
    int index = 0;
    int pin_state = 1;
    int current_state = wifi_state;

    int *current_seq = seq_connecting;

    while (1)
    {
        if (current_state != wifi_state)
        {
            current_state = wifi_state;
            index = 0;
            pin_state = 1;
            if (wifi_state == WIFI_STATE_CONNECTING)
            {
                current_seq = seq_connecting;
            }
            else if (wifi_state == WIFI_STATE_CONNECTED)
            {
                current_seq = seq_connected;
            }
            else if (wifi_state == WIFI_STATE_AP)
            {
                current_seq = seq_ap;
            }
            else
            {
                current_seq = 0;
            }
        }
        if (current_seq == 0)
        {
            vTaskDelay(500 / portTICK_PERIOD_MS);
            continue;
        }

        if (current_seq[index] == 0)
        {
            index = 0;
        }
        int val = current_seq[index];
        index++;

        gpio_set_pin(13, pin_state);
        if (pin_state)
            pin_state = 0;
        else
            pin_state = 1;
        vTaskDelay(val / portTICK_PERIOD_MS);
    }
}

static void set_static_ip()
{
    // TODO: Should handle errors where stuff is not found on NVS
    nvs_handle_t nvs_h;
    size_t nvs_str_size = 20;
    int err = nvs_open("storage", NVS_READONLY, &nvs_h);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error while opening NVS storage: %04X", err);
        return;
    }

    provision_config_t p_config;

    size_t data_size = sizeof(p_config);
    nvs_get_blob(nvs_h, "pconfig", &p_config, &data_size);
    nvs_close(nvs_h);

    if (!p_config.dhcp) {
        err = esp_netif_dhcpc_stop(sta_netif);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Unable to stop dhcp server: %04X", err);
            return;
        }

        esp_netif_ip_info_t ip;
        memset(&ip, 0, sizeof(esp_netif_ip_info_t));
        ip.ip.addr = ipaddr_addr(p_config.static_ip);
        ip.gw.addr = ipaddr_addr(p_config.gw);
        ip.netmask.addr = ipaddr_addr(p_config.subnet);
        err = esp_netif_set_ip_info(sta_netif, &ip);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Error while setting ip info: %04X", err);
            return;
        }
    }
}

void wifi_event_handler(void *arg, esp_event_base_t event_base, int event_id, void *event_data)
{
    wifi_mode_t mode;

    if (event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_get_mode(&mode);
        if (mode == WIFI_MODE_STA)
        {
            ESP_LOGI(TAG, "Connecting to the AP");
            wifi_state = WIFI_STATE_CONNECTING;
            esp_wifi_connect();
        }
    }
    else if (event_id == WIFI_EVENT_STA_CONNECTED)
    {
        wifi_state = WIFI_STATE_CONNECTED;
        set_static_ip();
    }
    else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
        wifi_state = WIFI_STATE_CONNECTING;
        esp_wifi_connect();
    }
}

void ip_event_handler(void *arg, esp_event_base_t event_base, int event_id, void *event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        /* Signal main application to continue execution */
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);
    }
}

void wait_wifi_connected()
{
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, false, true, portMAX_DELAY);
}

bool wifi_init()
{
    wifi_state = WIFI_STATE_NONE;
    xTaskCreate(wifi_led, "wifi_led", 4096, NULL, 5, NULL);

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));

    wifi_event_group = xEventGroupCreate();
    /* Initialize TCP/IP */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Initialize Wi-Fi including netif with default config */
    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_cfg;
    if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) != ESP_OK)
    {
        return false;
    }
    if (strlen((const char *)wifi_cfg.sta.ssid) == 0)
    {
        return false;
    }

    ESP_LOGI(TAG, "Provisioned with SSID: [%s]", wifi_cfg.sta.ssid);
    return true;
}

void wifi_start()
{
    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_set_sta()
{
    /* Start Wi-Fi in station mode */
    wifi_state = WIFI_STATE_CONNECTING;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
}

void wifi_set_ap()
{
    wifi_state = WIFI_STATE_AP;

    /* Start Wi-Fi in station mode */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
}
