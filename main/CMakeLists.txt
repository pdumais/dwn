idf_component_register(SRCS "app_main.c" "wifi.c" "provision.c" "gpio.c" "http_server.c" "api.c" "ota.c" "taskpool.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES "html1.html" "index.html" "style.css")
