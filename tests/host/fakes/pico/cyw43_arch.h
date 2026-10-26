#pragma once
#include <stdio.h>
#include "lwip/netif.h"
#define CYW43_COUNTRY_GERMANY 0
#define CYW43_AUTH_WPA2_AES_PSK 1
#define CYW43_AUTH_OPEN 0
#define CYW43_ITF_STA 0
#define CYW43_ITF_AP 1
#define CYW43_LINK_UP 3
#define CYW43_LINK_JOIN 1
#define CYW43_LINK_NOIP 2
#define CYW43_LINK_FAIL -1
#define CYW43_LINK_NONET -2
#define CYW43_LINK_BADAUTH -3
#define CYW43_NONE_PM 0
typedef struct { struct netif netif[2]; } cyw43_t;
extern cyw43_t cyw43_state; extern int FAKE_LINK;
static inline int cyw43_arch_init_with_country(int c){(void)c;return 0;}
static inline void cyw43_arch_enable_sta_mode(void){} static inline void cyw43_arch_disable_sta_mode(void){} static inline void cyw43_arch_disable_ap_mode(void){}
extern char FAKE_AP_SSID[40];
static inline void cyw43_arch_enable_ap_mode(const char*a,const char*b,int c){(void)b;(void)c; snprintf(FAKE_AP_SSID,sizeof FAKE_AP_SSID,"%s",a);}
extern int FAKE_CONNECTS, FAKE_LEAVES;
static inline int cyw43_arch_wifi_connect_async(const char*a,const char*b,int c){(void)a;(void)b;(void)c;FAKE_CONNECTS++;return 0;}
static inline int cyw43_tcpip_link_status(cyw43_t *s,int i){(void)s;(void)i;return FAKE_LINK;}
static inline void cyw43_arch_lwip_begin(void){} static inline void cyw43_arch_lwip_end(void){}
static inline int cyw43_wifi_leave(cyw43_t *s, int itf){(void)s;(void)itf;FAKE_LEAVES++;return 0;}
static inline int cyw43_wifi_pm(cyw43_t *s, unsigned pm){(void)s;(void)pm;return 0;}
