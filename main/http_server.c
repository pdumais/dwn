#include "http_server.h"
#include <esp_log.h>
#include "common.h"

static httpd_handle_t server = NULL;
static const char *TAG = "http_server_c";

static QueueHandle_t async_req_queue;
static SemaphoreHandle_t worker_ready_count;
static TaskHandle_t worker_handles[MAX_ASYNC_REQUESTS];

typedef struct {
    httpd_req_t* req;
    httpd_req_handler_t handler;
} httpd_async_req_t;

httpd_handle_t get_webserver()
{
    return server;
}

static jsonrpchandler jhandler = 0;

static esp_err_t jsonrpc_handler(httpd_req_t *req)
{
    char buf[200];

    // Parse the incoming payload
    httpd_req_recv(req, buf, 200);
    cJSON *root = cJSON_Parse(buf);
    if (!root)
    {
        return ESP_FAIL;
    }

    cJSON *method = cJSON_GetObjectItem(root, "method");
    cJSON *id = cJSON_GetObjectItem(root, "id");
    cJSON *params = cJSON_GetObjectItem(root, "params");

    cJSON *ret = cJSON_CreateObject();
    cJSON *result = cJSON_CreateObject();
    jhandler(method->valuestring, params, result);
    cJSON_AddItemToObject(ret, "result", result);
    cJSON_AddItemToObject(ret, "jsonrpc", cJSON_CreateString("2.0"));
    cJSON_AddItemToObject(ret, "id", cJSON_CreateString(id->valuestring));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, cJSON_Print(ret), HTTPD_RESP_USE_STRLEN);
    httpd_resp_send(req, 0, 0);

    cJSON_Delete(result);
    cJSON_Delete(root);
    return ESP_OK;
}

void register_jsonrpc_handler(jsonrpchandler handler)
{
    jhandler = handler;
}

void stop_webserver()
{
    if (server)
    {
        /* Stop the httpd server */
        httpd_stop(server);
    }
}

esp_err_t submit_async_req(httpd_req_t *req, httpd_req_handler_t handler)
{
    httpd_req_t* copy = 0;
    esp_err_t err = httpd_req_async_handler_begin(req, &copy);
    if (err != ESP_OK) return err;

    httpd_async_req_t async_req = {
        .req = copy,
        .handler = handler,
    };

    int ticks = 0;

    if (xSemaphoreTake(worker_ready_count, ticks) == false) {
        ESP_LOGE(TAG, "No workers are available");
        httpd_req_async_handler_complete(copy); // cleanup
        return ESP_FAIL;
    }

    if (xQueueSend(async_req_queue, &async_req, pdMS_TO_TICKS(100)) == false) {
        ESP_LOGE(TAG, "worker queue is full");
        httpd_req_async_handler_complete(copy); // cleanup
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void async_req_worker_task(void *p)
{
    ESP_LOGI(TAG, "starting async req task worker");

    while (true) {
        xSemaphoreGive(worker_ready_count);

        httpd_async_req_t async_req;
        if (xQueueReceive(async_req_queue, &async_req, 10)) {
            ESP_LOGI(TAG, "invoking %s", async_req.req->uri);
            async_req.handler(async_req.req);
            httpd_req_async_handler_complete(async_req.req);
        }
    }

    ESP_LOGW(TAG, "worker stopped");
    vTaskDelete(NULL);
}

static void start_async_req_workers(void)
{
    worker_ready_count = xSemaphoreCreateCounting(MAX_ASYNC_REQUESTS, 0);
    async_req_queue = xQueueCreate(1, sizeof(httpd_async_req_t));

    for (int i = 0; i < MAX_ASYNC_REQUESTS; i++) 
    {
        xTaskCreate(async_req_worker_task, "async_req_worker",4096, 0 , 5, &worker_handles[i]);
    }
//TODO: must also delete those tasks
}


void start_webserver()
{
    start_async_req_workers();

    /* Generate default configuration */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    /* Start the httpd server */
    int err = httpd_start(&server, &config);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "start_webserver error: %04X", err);
        return;
    }
    
    // The jsonrpc endpoint is used by the main API
    httpd_uri_t h;
    h.uri = "/jsonrpc";
    h.method = HTTP_POST;
    h.handler = jsonrpc_handler;
    h.user_ctx = 0;
    httpd_register_uri_handler(server, &h);
}

// This is used for the Provisioning module
void register_uri(const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *r))
{
    httpd_uri_t h;
    h.uri = uri;
    h.method = method;
    h.handler = handler;
    h.user_ctx = 0;

    httpd_register_uri_handler(server, &h);
}