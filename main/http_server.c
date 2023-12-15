#include "http_server.h"
#include <esp_log.h>
#include "common.h"
#include "taskpool.h"

static httpd_handle_t server = NULL;
static const char *TAG = "http_server_c";

typedef struct
{
    int fd;
    size_t len;
    char data[];
} ws_client_t;

static ws_client_t ws_clients[MAX_WS_CLIENTS];

httpd_handle_t get_webserver()
{
    return server;
}

static jsonrpchandler jhandler = 0;

static void async_ws_send(ws_client_t *swc)
{
    int fd = swc->fd;
    httpd_ws_frame_t pkt;
    memset(&pkt, 0, sizeof(httpd_ws_frame_t));
    pkt.payload = (uint8_t *)swc->data;
    pkt.len = swc->len;
    pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame_async(server, fd, &pkt);

    free(swc);
}

void broadcast_ws(const char *data)
{
    int fds[MAX_WS_CLIENTS] = {0};
    size_t client_count = MAX_WS_CLIENTS;
    size_t len = strlen(data);
    esp_err_t err = httpd_get_client_list(server, &client_count, (int *)&fds);
    for (int i = 0; i < client_count; i++)
    {
        int fd = fds[i];
        if (httpd_ws_get_fd_info(server, fd) == HTTPD_WS_CLIENT_WEBSOCKET)
        {
            ws_client_t *swc = malloc(sizeof(ws_client_t) + len + 1);
            swc->fd = fd;
            swc->len = len;
            strcpy(swc->data, data);
            if (httpd_queue_work(server, async_ws_send, swc) != ESP_OK)
            {
                free(swc);
            }
        }
    }
}

static esp_err_t style_handler(httpd_req_t *req)
{
    extern const unsigned char style_start[] asm("_binary_style_css_start");
    extern const unsigned char style_end[] asm("_binary_style_css_end");
    const size_t style_size = (style_end - style_start);
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, (const char *)style_start, style_size);
    return ESP_OK;
}


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

typedef struct
{
    httpd_req_t *req;
    httpd_req_handler_t handler;
} async_http_handler_t;

esp_err_t http_async_handler(async_http_handler_t *arg)
{
    arg->handler(arg->req);
    httpd_req_async_handler_complete(arg->req);
    free(arg);

    return ESP_OK;
}

esp_err_t submit_async_req(httpd_req_t *req, httpd_req_handler_t handler)
{
    httpd_req_t *copy = 0;
    esp_err_t err = httpd_req_async_handler_begin(req, &copy);
    if (err != ESP_OK)
        return err;

    async_http_handler_t *ah = malloc(sizeof(async_http_handler_t));
    ah->req = copy;
    ah->handler = handler;

    if (schedule_task(http_async_handler, ah) != ESP_OK)
    {
        httpd_req_async_handler_complete(copy); // cleanup
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
        return ESP_OK;

    uint8_t buf[256] = {0};
    httpd_ws_frame_t pkt;
    pkt.payload = buf;
    httpd_ws_recv_frame(req, &pkt, sizeof(buf) - 1);
    if (!pkt.len)
        return ESP_FAIL;

    buf[pkt.len] = 0;

    return ESP_OK;
}

void init_ws()
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
        ws_clients[i].fd = 0;

    httpd_uri_t h = {0};
    h.uri = "/ws";
    h.method = HTTP_GET;
    h.handler = ws_handler;
    h.user_ctx = 0;
    h.is_websocket = true;
    h.handle_ws_control_frames = false;
    h.supported_subprotocol = "jsonrpc";

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
    httpd_uri_t h = {
        .uri = "/jsonrpc",
        .method = HTTP_POST,
        .handler = jsonrpc_handler,
        .user_ctx = 0,
    };
    httpd_register_uri_handler(server, &h);

    register_uri("/style.css", HTTP_GET, style_handler);
}

