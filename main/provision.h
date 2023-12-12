#pragma once

typedef struct {
    char static_ip[20];
    char subnet[20];
    char gw[20];
    char remote_server[20];
    bool dhcp;
    bool active;
} __attribute__((packed)) provision_config_t;

void provision_init();