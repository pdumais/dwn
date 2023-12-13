#include "http_server.h"
#include <esp_log.h>
#include "common.h"
#include "taskpool.h"

static httpd_handle_t server = NULL;
static const char *TAG = "http_server_c";



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
        stop_pool();
        /* Stop the httpd server */
        httpd_stop(server);
    }
}

struct async_http_handler_t {
    httpd_req_t* req;
    httpd_req_handler_t handler;
};

esp_err_t http_async_handler(async_http_handler_t *arg)
{
    arg->handler(arg->req);
    httpd_req_async_handler_complete(arg->req);
    free(arg);
   
}

esp_err_t submit_async_req(httpd_req_t *req, httpd_req_handler_t handler)
{
    httpd_req_t* copy = 0;
    esp_err_t err = httpd_req_async_handler_begin(req, &copy);
    if (err != ESP_OK) return err;

    async_http_handler_t *ah = malloc(sizeof(async_http_handler_t));
    ah->req = req;
    ah->handler = handler;

    if (schedule_task(http_async_handler, ah) != ESP_OK) {
        httpd_req_async_handler_complete(copy); // cleanup
        return ESP_FAIL;
    }

    return ESP_OK;
}




void start_webserver()
{
    start_pool();

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