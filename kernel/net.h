#ifndef KAEL_NET_H
#define KAEL_NET_H
#include "types.h"

/* Polling waits run with IRQ0 enabled; sleep until the next timer/input IRQ. */
static inline void net_idle_halt(void) { asm volatile("hlt"); }

/* ---- io port helpers ---- */
static inline void outb(uint16_t p, uint8_t v) { asm volatile("outb %0,%1" : : "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t r; asm volatile("inb %1,%0" : "=a"(r) : "Nd"(p)); return r; }
static inline void outw(uint16_t p, uint16_t v) { asm volatile("outw %0,%1" : : "a"(v), "Nd"(p)); }
static inline uint16_t inw(uint16_t p) { uint16_t r; asm volatile("inw %1,%0" : "=a"(r) : "Nd"(p)); return r; }
static inline void outl(uint16_t p, uint32_t v) { asm volatile("outl %0,%1" : : "a"(v), "Nd"(p)); }
static inline uint32_t inl(uint16_t p) { uint32_t r; asm volatile("inl %1,%0" : "=a"(r) : "Nd"(p)); return r; }
static inline void io_wait(void) { outb(0x80, 0); }

/* ---- small string helpers ---- */
static inline uint32_t kstrlen(const char* s) { uint32_t n = 0; while (s[n]) n++; return n; }
static inline int kstrcmp(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return (unsigned char)*a - (unsigned char)*b; a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static inline void kstrcpy(char* d, const char* s) { while (*s) *d++ = *s++; *d = 0; }
static inline void kstrcat(char* d, const char* s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }
static inline int kstarts(const char* s, const char* p) {
    while (*p) { if (*s != *p) return 0; s++; p++; } return 1;
}
static inline int kci_eq(char a, char b) {
    if (a >= 'A' && a <= 'Z') a += 32;
    if (b >= 'A' && b <= 'Z') b += 32;
    return a == b;
}
static inline int kci_starts(const char* s, const char* p) {
    while (*p) { if (!kci_eq(*s, *p)) return 0; s++; p++; } return 1;
}

/* ---- byte order ---- */
static inline uint16_t htons(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static inline uint32_t htonl(uint32_t v) {
    return (v << 24) | ((v << 8) & 0xFF0000u) | ((v >> 8) & 0xFF00u) | (v >> 24);
}
#define ntohs htons
#define ntohl htonl

/* ---- mem (klib.c) ---- */
void* memset(void* d, int c, size_t n);
void* memcpy(void* d, const void* s, size_t n);
int memcmp(const void* a, const void* b, size_t n);

/* ---- checksums (internet, one's complement) ---- */
uint32_t cksum_add(const uint8_t* p, uint32_t len, uint32_t sum);
uint16_t cksum_fold(uint32_t sum);

/* ---- timer ---- */
void pit_init(void);
uint32_t get_ticks(void);

/* ---- serial debug (COM1) ---- */
void serial_init(void);
void serial_puts(const char* s);

/* ---- pci ---- */
uint32_t pci_cfg_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
void pci_cfg_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val);
int pci_find_id(uint16_t vid, uint16_t did, uint8_t* bus, uint8_t* slot);
int pci_find(uint16_t vid, uint16_t did, uint8_t* bus, uint8_t* slot, uint16_t* io_base);
int pci_list_nics(char lines[][72], int max);

/* Marvell Yukon-2 polling driver. */
int sky2_init(void);
int sky2_send(const uint8_t* frame, uint32_t len);
int sky2_receive(uint8_t* frame, uint32_t max);
uint8_t* sky2_mac(void);
int sky2_link_up(void);
void sky2_shutdown(void);
int sky2_wait_link(uint32_t timeout_ms);
void sky2_debug_dump(void);
void sky2_debug_link(void);
extern uint32_t g_sky2_status_les, g_sky2_rx_status, g_sky2_rx_ok;
extern uint32_t g_sky2_rx_bad, g_sky2_last_bad, g_sky2_tx_submit, g_sky2_tx_reports;
extern uint32_t g_sky2_diag_valid, g_sky2_diag_chip, g_sky2_diag_rev, g_sky2_diag_ram_kb;
extern uint32_t g_sky2_diag_gp_stat, g_sky2_diag_gp_ctrl, g_sky2_diag_phy11;
extern uint32_t g_sky2_diag_rx_uc, g_sky2_diag_rx_bc, g_sky2_diag_rx_mc, g_sky2_diag_rx_fcs;

/* ---- rtl8139 nic (poll driven) ---- */
int rtl8139_init(void);
int rtl8139_send(const uint8_t* frame, uint32_t len);
int rtl8139_receive(uint8_t* buf, uint32_t max);
uint8_t* rtl8139_mac(void);
void rtl8139_shutdown(void);

/* ---- net core ---- */
extern uint32_t g_ip, g_mask, g_gw, g_dns;   /* host byte order */
extern uint8_t  g_mac[6];
extern int g_have_ip, g_nic_ok;
extern int g_sky2_found, g_sky2_init_error;
void net_init(void);
void net_shutdown(void);
void net_poll(void);
int  net_parse_ip(const char* s, uint32_t* ip);
void net_ip_str(uint32_t ip, char* out);
int  net_dhcp(char* err);
int  net_set_static(uint32_t ip, uint32_t mask, uint32_t gw, uint32_t dns);
int  net_resolve(const char* name, uint32_t* ip, char* err);
int  net_ping(uint32_t ip, uint32_t* rtt_ms);
const char* net_nic_name(void);

/* net.c <-> tcp.c glue */
int ip_send_raw(uint32_t dst_ip, uint8_t proto, const uint8_t* seg, uint32_t seglen);
void tcp_input(const uint8_t* frame, uint32_t len);
void tcp_poll(void);

/* ---- tcp (single blocking connection) ---- */
int  tcp_connect(uint32_t ip, uint16_t port, char* err);
int  tcp_send(const void* data, uint32_t len, char* err);
int  tcp_recv(void* buf, uint32_t max, uint32_t timeout_ticks, char* err);
void tcp_close(void);

/* ---- http + html ---- */
int http_fetch(const char* url, char* out, uint32_t cap, uint32_t* out_len,
               int* status, char* err);
int html_to_lines(const char* html, char lines[][76], int max_lines, int* count,
                  char* title, int title_cap);

#endif
