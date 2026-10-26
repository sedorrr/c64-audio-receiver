/* Functions of net.c used by the other network modules (ota.c). */
#pragma once
void  net_post_message(const char *m);          /* a line for the diagnostics log */
char *net_body_buffer(void);                    /* the shared request body buffer (>= 32 KB + 4 KB for ota_apply) */
