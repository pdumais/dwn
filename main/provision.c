#include <string.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event_base.h>
#include <esp_http_server.h>
#include "wifi.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "http_server.h"
#include "common.h"
#include "provision.h"

typedef enum
{
    PROV_COMPLETE,
    PROV_ERROR,
} http_prov_event_type_t;

ESP_EVENT_DEFINE_BASE(PROV_EVENT);

const int SCAN_DONE_EVENT = BIT0;
static EventGroupHandle_t scan_event_group;
static const char *TAG = "provision_c";
static httpd_handle_t server = NULL;
static wifi_config_t wifi_config = {0};

static void on_scan_done(void *arg, esp_event_base_t event_base, int event_id, void *event_data)
{
    xEventGroupSetBits(scan_event_group, SCAN_DONE_EVENT);
}

static esp_err_t prov_get_handler(httpd_req_t *req)
{
    extern const unsigned char html1_start[] asm("_binary_html1_html_start");
    extern const unsigned char html1_end[] asm("_binary_html1_html_end");
    const size_t html_size = (html1_end - html1_start);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)html1_start, html_size);
    return ESP_OK;
}

static esp_err_t prov_post_handler(httpd_req_t *req)
{
    char buf[200];
    provision_config_t p_config;

    // Parse the incoming payload
    httpd_req_recv(req, buf, 200);
    cJSON *root = cJSON_Parse(buf);
    if (!root)
        return ESP_FAIL;
    cJSON *wifi = cJSON_GetObjectItem(root, "wifi");
    if (!wifi)
        return ESP_FAIL;
    cJSON *config = cJSON_GetObjectItem(root, "config");
    if (!config)
        return ESP_FAIL;

    p_config.dhcp = cJSON_IsTrue(cJSON_GetObjectItem(config, "dhcp"));
    p_config.active = cJSON_IsTrue(cJSON_GetObjectItem(config, "active"));

    cJSON *j_ssid = cJSON_GetObjectItem(wifi, "ssid");
    cJSON *j_password = cJSON_GetObjectItem(wifi, "password");
    if (!j_ssid)
        return ESP_FAIL;
    if (!j_password)
        return ESP_FAIL;
    char *ssid = j_ssid->valuestring;
    char *password = j_password->valuestring;
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

    if (!p_config.dhcp)
    {
        cJSON *j_ip = cJSON_GetObjectItem(config, "ip");
        cJSON *j_gw = cJSON_GetObjectItem(config, "gw");
        if (!j_ip)
            return ESP_FAIL;
        if (!j_gw)
            return ESP_FAIL;
        char *ip = j_ip->valuestring;
        char *gw = j_gw->valuestring;
        strlcpy((char *)p_config.static_ip, ip, 20);
        strlcpy((char *)p_config.gw, gw, 20);
        strlcpy((char *)p_config.subnet, "255.255.255.0", 20);
        ESP_LOGI(TAG, "Static config received: ip = [%s], gw = [%s]", p_config.static_ip, p_config.gw);
    }
    if (p_config.active)
    {
        cJSON *j_remote_server = cJSON_GetObjectItem(config, "remote_server");
        if (!j_remote_server)
            return ESP_FAIL;
        char *remote_server = j_remote_server->valuestring;
        strlcpy((char *)p_config.remote_server, remote_server, 20);
    }

    ESP_LOGI(TAG, "Wifi config received: ssid = [%s], password = [%s]", ssid, password);

    // Store the static IP in NVS
    size_t data_size = sizeof(p_config);
    nvs_handle_t nvs_h;
    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &nvs_h));
    nvs_set_blob(nvs_h, "pconfig", &p_config, data_size);
    ESP_ERROR_CHECK(nvs_commit(nvs_h));
    nvs_close(nvs_h);

    cJSON_Delete(root);

    httpd_resp_set_status(req, "202 ACCEPTED");
    httpd_resp_send(req, 0, 0);
    esp_event_post(PROV_EVENT, PROV_COMPLETE, 0, 0, 0);
    return ESP_OK;
}

static esp_err_t prov_get_data_handler(httpd_req_t *req)
{
    wifi_config_t wconfig;
    provision_config_t p_config;

    ESP_LOGI(TAG, "Start get_data response");

    esp_wifi_get_config(WIFI_IF_STA, &wconfig);

    // Store the static IP in NVS
    size_t data_size = sizeof(provision_config_t);
    nvs_handle_t nvs_h;
    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &nvs_h));
    nvs_get_blob(nvs_h, "pconfig", &p_config, &data_size);
    nvs_close(nvs_h);

    cJSON *root = cJSON_CreateObject();
    cJSON *wifi = cJSON_CreateObject();
    cJSON *config = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "wifi", wifi);
    cJSON_AddItemToObject(root, "config", config);
    cJSON_AddItemToObject(wifi, "ssid", cJSON_CreateString((char *)wconfig.sta.ssid));
    cJSON_AddItemToObject(config, "ip", cJSON_CreateString(p_config.static_ip));
    cJSON_AddItemToObject(config, "gw", cJSON_CreateString(p_config.gw));
    cJSON_AddItemToObject(config, "remote_server", cJSON_CreateString(p_config.remote_server));
    cJSON_AddItemToObject(config, "dhcp", cJSON_CreateBool(p_config.dhcp));
    cJSON_AddItemToObject(config, "active", cJSON_CreateBool(p_config.active));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, cJSON_Print(root), HTTPD_RESP_USE_STRLEN);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "End get_data response");

    return ESP_OK;
}

void on_prov_event(void *arg, esp_event_base_t event_base, int event_id, void *event_data)
{
    if (event_id == PROV_COMPLETE)
    {
        stop_webserver();
        wifi_set_sta();
        ESP_LOGI(TAG, "Provisioning Complete");
        int err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Error while trying to set config: %i", err);
        }
        esp_wifi_connect();
    }
}

/*static esp_err_t async_prov_get_ap_handler(httpd_req_t *req)
{
    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
    wifi_scan_config_t scan_config = {
        .ssid = 0,
        .bssid = 0,
        .channel = 0,
        .scan_type = WIFI_SCAN_TYPE_PASSIVE,
        .show_hidden = true
    };

    ESP_LOGI(TAG, "Starting AP scan");
    esp_wifi_scan_start(&scan_config, false);
    xEventGroupClearBits(scan_event_group, SCAN_DONE_EVENT);
    xEventGroupWaitBits(scan_event_group, SCAN_DONE_EVENT, false, true, portMAX_DELAY);

    ESP_LOGI(TAG, "AP scan Done");

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));

    cJSON *root = cJSON_CreateObject();
    cJSON *applist = cJSON_CreateArray();

    ESP_LOGI(TAG, "%i AP found", number);
    for (int i = 0; i < number; i++)
    {
        cJSON_AddItemToArray(applist, cJSON_CreateString((char *)ap_info[i].ssid));
    }

    cJSON_AddItemToObject(root, "ap", applist);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, cJSON_Print(root), HTTPD_RESP_USE_STRLEN);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "End AP response");

    return ESP_OK;
}*/

/*static esp_err_t prov_get_ap_handler(httpd_req_t *req)
{
    return submit_async_req(req, async_prov_get_ap_handler);
}*/

void provision_init()
{
    scan_event_group = xEventGroupCreate();

    ESP_LOGI(TAG, "Provisioning Start");
    // ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &on_scan_done, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROV_EVENT, PROV_COMPLETE, &on_prov_event, NULL));

    start_webserver();
    register_uri("/", HTTP_GET, prov_get_handler);
    register_uri("/data", HTTP_POST, prov_post_handler);
    register_uri("/data", HTTP_GET, prov_get_data_handler);
    // register_uri("/ap", HTTP_GET, prov_get_ap_handler);
}