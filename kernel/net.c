#include "net.h"

uint32_t g_ip, g_mask, g_gw, g_dns;      /* host byte order */
uint8_t  g_mac[6];
int g_have_ip = 0, g_nic_ok = 0;
int g_sky2_found = 0, g_sky2_init_error = 0;

static uint32_t ip_id = 1;
static uint8_t  pkt[1514];               /* single outbound frame buffer */

/* Keep protocol code independent of a particular NIC implementation. */
typedef struct {
    const char* name;
    int (*send)(const uint8_t* frame, uint32_t len);
    int (*receive)(uint8_t* frame, uint32_t max);
    uint8_t* (*mac)(void);
} net_nic_ops_t;

static const net_nic_ops_t rtl8139_ops = {
    "RTL8139", rtl8139_send, rtl8139_receive, rtl8139_mac
};
static const net_nic_ops_t sky2_ops = {
    "Marvell Yukon-2 (sky2)", sky2_send, sky2_receive, sky2_mac
};
static const net_nic_ops_t* active_nic = 0;

const char* net_nic_name(void) {
    return active_nic ? active_nic->name : 0;
}

static int nic_send(const uint8_t* frame, uint32_t len) {
    return active_nic ? active_nic->send(frame, len) : -1;
}

/* ---------------- checksums ---------------- */
uint32_t cksum_add(const uint8_t* p, uint32_t len, uint32_t sum) {
    while (len > 1) { sum += ((uint32_t)p[0] << 8) | p[1]; p += 2; len -= 2; }
    if (len) sum += (uint32_t)p[0] << 8;
    return sum;
}

uint16_t cksum_fold(uint32_t sum) {
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* ---------------- tiny helpers ---------------- */
static void u32dec(uint32_t v, char* o) {
    char t[12]; int i = 0;
    if (v == 0) { o[0] = '0'; o[1] = 0; return; }
    while (v) { t[i++] = (char)('0' + (v % 10)); v /= 10; }
    for (int j = 0; j < i; j++) o[j] = t[i - 1 - j];
    o[i] = 0;
}

int net_parse_ip(const char* s, uint32_t* ip) {
    uint32_t v = 0, cur = 0;
    int parts = 0, digits = 0;
    for (;;) {
        char c = *s;
        if (c >= '0' && c <= '9') {
            cur = cur * 10 + (uint32_t)(c - '0');
            if (cur > 255) return 0;
            digits++; s++;
        } else if (c == '.' || c == 0 || c == ' ') {
            if (!digits) return 0;
            v = (v << 8) | cur; parts++; cur = 0; digits = 0;
            if (parts > 4) return 0;
            if (c == 0 || c == ' ') break;
            s++;
        } else return 0;
    }
    if (parts != 4) return 0;
    *ip = v;
    return 1;
}

void net_ip_str(uint32_t ip, char* out) {
    char tmp[4];
    u32dec((ip >> 24) & 0xFF, tmp); kstrcpy(out, tmp); kstrcat(out, ".");
    u32dec((ip >> 16) & 0xFF, tmp); kstrcat(out, tmp); kstrcat(out, ".");
    u32dec((ip >> 8) & 0xFF, tmp);  kstrcat(out, tmp); kstrcat(out, ".");
    u32dec(ip & 0xFF, tmp);         kstrcat(out, tmp);
}

/* ---------------- ARP ---------------- */
#define ARP_N 8
static struct { uint32_t ip; uint8_t mac[6]; uint8_t valid; } arp_tab[ARP_N];
static int arp_round = 0;
static uint32_t arp_wait_ip;
static uint8_t  arp_wait_mac[6];
static int      arp_got = 0;
static int      arp_busy = 0;

static void arp_learn(uint32_t ip, const uint8_t* m) {
    if (ip == 0) return;
    for (int i = 0; i < ARP_N; i++)
        if (arp_tab[i].valid && arp_tab[i].ip == ip) {
            memcpy(arp_tab[i].mac, m, 6); return;
        }
    for (int i = 0; i < ARP_N; i++)
        if (!arp_tab[i].valid) {
            arp_tab[i].valid = 1; arp_tab[i].ip = ip;
            memcpy(arp_tab[i].mac, m, 6); return;
        }
    arp_tab[arp_round].valid = 1; arp_tab[arp_round].ip = ip;
    memcpy(arp_tab[arp_round].mac, m, 6);
    arp_round = (arp_round + 1) % ARP_N;
}

static void arp_send_req(uint32_t ip) {
    uint8_t b[64];
    memset(b, 0, sizeof b);
    memset(b, 0xFF, 6);                          /* eth dst broadcast */
    memcpy(b + 6, g_mac, 6);
    b[12] = 0x08; b[13] = 0x06;                  /* ARP */
    b[14] = 0; b[15] = 1;                        /* htype ethernet */
    b[16] = 0x08; b[17] = 0x00;                  /* ptype IPv4 */
    b[18] = 6; b[19] = 4;
    b[20] = 0; b[21] = 1;                        /* op request */
    memcpy(b + 22, g_mac, 6);
    uint32_t s = htonl(g_ip);
    memcpy(b + 28, &s, 4);
    uint32_t t = htonl(ip);
    memcpy(b + 38, &t, 4);
    nic_send(b, 60);
}

static void arp_input(const uint8_t* p, uint32_t len) {
    if (len < 42) return;
    uint16_t op = (uint16_t)((p[20] << 8) | p[21]);
    uint32_t spa = ((uint32_t)p[28] << 24) | ((uint32_t)p[29] << 16) |
                   ((uint32_t)p[30] << 8) | p[31];
    uint32_t tpa = ((uint32_t)p[38] << 24) | ((uint32_t)p[39] << 16) |
                   ((uint32_t)p[40] << 8) | p[41];
    if (spa) arp_learn(spa, p + 22);

    if (op == 2 && spa == arp_wait_ip) {
        memcpy(arp_wait_mac, p + 22, 6);
        arp_got = 1;
    }
    if (op == 1 && g_have_ip && tpa == g_ip) {
        /* reply to whoever asked for us */
        uint8_t b[64];
        memset(b, 0, sizeof b);
        memcpy(b, p + 22, 6);                    /* eth dst = requester */
        memcpy(b + 6, g_mac, 6);
        b[12] = 0x08; b[13] = 0x06;
        b[14] = 0; b[15] = 1;
        b[16] = 0x08; b[17] = 0x00;
        b[18] = 6; b[19] = 4;
        b[20] = 0; b[21] = 2;                    /* op reply */
        memcpy(b + 22, g_mac, 6);
        uint32_t s = htonl(g_ip);
        memcpy(b + 28, &s, 4);
        memcpy(b + 32, p + 22, 6);               /* tha = requester mac */
        memcpy(b + 38, &s, 4);                   /* tpa = our ip (spa==tpa for request) */
        /* requester's ip is spa: */
        uint32_t rq = htonl(spa);
        memcpy(b + 38, &rq, 4);
        nic_send(b, 60);
    }
}

static int arp_resolve(uint32_t ip, uint8_t mac[6]) {
    if (ip == 0xFFFFFFFFu) { memset(mac, 0xFF, 6); return 0; }
    for (int i = 0; i < ARP_N; i++)
        if (arp_tab[i].valid && arp_tab[i].ip == ip) {
            memcpy(mac, arp_tab[i].mac, 6); return 0;
        }
    if (arp_busy) return -1;                     /* no nested resolve */
    arp_busy = 1;
    arp_wait_ip = ip; arp_got = 0;
    uint32_t start = get_ticks(), last = 0;
    while (!arp_got && (int32_t)(get_ticks() - start) < 2000) {
        if ((int32_t)(get_ticks() - last) >= 500) {
            arp_send_req(ip); last = get_ticks();
        }
        net_poll();
        net_idle_halt();
    }
    arp_busy = 0;
    if (!arp_got) { serial_puts("[arp] resolve timeout\n"); return -1; }
    memcpy(mac, arp_wait_mac, 6);
    arp_learn(ip, mac);
    return 0;
}

/* ---------------- IP ---------------- */
static int ip_build_send(uint32_t dst_ip, const uint8_t* dst_mac, uint8_t proto,
                         const uint8_t* payload, uint32_t len, uint32_t src_ip) {
    uint8_t mac[6];
    if (dst_mac) {
        memcpy(mac, dst_mac, 6);
    } else {
        uint32_t nh = ((dst_ip & g_mask) == (g_ip & g_mask)) ? dst_ip : g_gw;
        if (arp_resolve(nh, mac) < 0) return -2;
    }
    if (len > 1480) return -1;

    memcpy(pkt, mac, 6);
    memcpy(pkt + 6, g_mac, 6);
    pkt[12] = 0x08; pkt[13] = 0x00;

    uint8_t* ip = pkt + 14;
    uint16_t tot = (uint16_t)(20 + len);
    memset(ip, 0, 20);
    ip[0] = 0x45;
    ip[2] = (uint8_t)(tot >> 8); ip[3] = (uint8_t)(tot & 0xFF);
    ip[4] = (uint8_t)(ip_id >> 8); ip[5] = (uint8_t)(ip_id & 0xFF); ip_id++;
    ip[8] = 64; ip[9] = proto;
    uint32_t s = htonl(src_ip), d = htonl(dst_ip);
    memcpy(ip + 12, &s, 4);
    memcpy(ip + 16, &d, 4);
    uint16_t cks = cksum_fold(cksum_add(ip, 20, 0));
    ip[10] = (uint8_t)(cks >> 8); ip[11] = (uint8_t)(cks & 0xFF);

    memcpy(pkt + 34, payload, len);
    return nic_send(pkt, 34 + len) < 0 ? -1 : 0;
}

int ip_send_raw(uint32_t dst_ip, uint8_t proto, const uint8_t* seg, uint32_t seglen) {
    if (!g_nic_ok || !g_have_ip) return -1;
    return ip_build_send(dst_ip, 0, proto, seg, seglen, g_ip);
}

/* ---------------- ICMP ---------------- */
static uint16_t ping_id, ping_seq;
static int      ping_got = 0;
static uint32_t ping_tick = 0;

static int icmp_send_echo(uint32_t dst, uint16_t id, uint16_t seq) {
    uint8_t b[72];
    memset(b, 0, sizeof b);
    b[0] = 8;                                    /* echo request */
    b[4] = (uint8_t)(id >> 8); b[5] = (uint8_t)(id & 0xFF);
    b[6] = (uint8_t)(seq >> 8); b[7] = (uint8_t)(seq & 0xFF);
    for (int i = 0; i < 64; i++) b[8 + i] = (uint8_t)('a' + (i % 26));
    uint16_t cks = cksum_fold(cksum_add(b, 72, 0));
    b[2] = (uint8_t)(cks >> 8); b[3] = (uint8_t)(cks & 0xFF);
    return ip_build_send(dst, 0, 1, b, 72, g_ip);
}

static void icmp_input(const uint8_t* p, uint32_t len, uint32_t src,
                       const uint8_t* eth_src) {
    if (len < 8) return;
    if (p[0] == 8 && g_have_ip) {                /* echo request -> reply */
        uint8_t b[1400];
        if (len > sizeof b) len = sizeof b;
        memcpy(b, p, len);
        b[0] = 0; b[2] = 0; b[3] = 0;
        uint16_t cks = cksum_fold(cksum_add(b, len, 0));
        b[2] = (uint8_t)(cks >> 8); b[3] = (uint8_t)(cks & 0xFF);
        ip_build_send(src, eth_src, 1, b, len, g_ip);
    } else if (p[0] == 0) {                      /* echo reply */
        uint16_t id = (uint16_t)((p[4] << 8) | p[5]);
        uint16_t sq = (uint16_t)((p[6] << 8) | p[7]);
        if (id == ping_id && sq == ping_seq) { ping_got = 1; ping_tick = get_ticks(); }
    }
}

/* ---------------- UDP ---------------- */
static int udp_send(uint32_t dst, uint16_t sport, uint16_t dport,
                    const uint8_t* data, uint32_t len,
                    uint32_t src_ip, const uint8_t* dst_mac) {
    uint8_t b[600];
    if (len > 592) return -1;
    b[0] = (uint8_t)(sport >> 8); b[1] = (uint8_t)(sport & 0xFF);
    b[2] = (uint8_t)(dport >> 8); b[3] = (uint8_t)(dport & 0xFF);
    uint16_t ulen = (uint16_t)(8 + len);
    b[4] = (uint8_t)(ulen >> 8); b[5] = (uint8_t)(ulen & 0xFF);
    b[6] = 0; b[7] = 0;
    memcpy(b + 8, data, len);

    uint32_t s = htonl(src_ip), d = htonl(dst);
    uint8_t ph[4] = { 0, 17, (uint8_t)(ulen >> 8), (uint8_t)(ulen & 0xFF) };
    uint32_t sum = cksum_add((uint8_t*)&s, 4, 0);
    sum = cksum_add((uint8_t*)&d, 4, sum);
    sum = cksum_add(ph, 4, sum);
    sum = cksum_add(b, ulen, sum);
    uint16_t cks = cksum_fold(sum);
    if (cks == 0) cks = 0xFFFF;
    b[6] = (uint8_t)(cks >> 8); b[7] = (uint8_t)(cks & 0xFF);

    return ip_build_send(dst, dst_mac, 17, b, ulen, src_ip);
}

/* ---------------- DHCP ---------------- */
static uint32_t dhcp_xid;
static uint32_t dhcp_offer_ip = 0, dhcp_offer_server = 0;
static uint32_t dhcp_ack_ip = 0, dhcp_mask = 0, dhcp_router = 0, dhcp_dns = 0;
static int dhcp_got_offer = 0, dhcp_got_ack = 0, dhcp_got_nak = 0;

static void dhcp_send(int msg_type) {
    uint8_t b[340];
    memset(b, 0, sizeof b);
    b[0] = 1; b[1] = 1; b[2] = 6;                /* bootrequest, eth, hlen6 */
    b[4] = (uint8_t)(dhcp_xid >> 24); b[5] = (uint8_t)(dhcp_xid >> 16);
    b[6] = (uint8_t)(dhcp_xid >> 8);  b[7] = (uint8_t)(dhcp_xid);
    b[10] = 0x80;                                /* broadcast flag */
    memcpy(b + 28, g_mac, 6);                    /* chaddr */
    b[236] = 0x63; b[237] = 0x82; b[238] = 0x53; b[239] = 0x63;  /* magic */
    int p = 240;
    b[p++] = 53; b[p++] = 1; b[p++] = (uint8_t)msg_type;
    if (msg_type == 3) {                         /* REQUEST */
        b[p++] = 50; b[p++] = 4;
        b[p++] = (uint8_t)(dhcp_offer_ip >> 24); b[p++] = (uint8_t)(dhcp_offer_ip >> 16);
        b[p++] = (uint8_t)(dhcp_offer_ip >> 8);  b[p++] = (uint8_t)(dhcp_offer_ip);
        b[p++] = 54; b[p++] = 4;
        b[p++] = (uint8_t)(dhcp_offer_server >> 24); b[p++] = (uint8_t)(dhcp_offer_server >> 16);
        b[p++] = (uint8_t)(dhcp_offer_server >> 8);  b[p++] = (uint8_t)(dhcp_offer_server);
    }
    b[p++] = 55; b[p++] = 3; b[p++] = 1; b[p++] = 3; b[p++] = 6;  /* ask mask/gw/dns */
    b[p++] = 255;
    if (p < 300) p = 300;                       /* minimum BOOTP/DHCP payload */

    uint8_t bcast[6];
    memset(bcast, 0xFF, 6);
    udp_send(0xFFFFFFFFu, 68, 67, b, (uint32_t)p, 0, bcast);
}

static void dhcp_input(const uint8_t* p, uint32_t len) {
    if (len < 244) return;
    if (p[0] != 2) return;                       /* bootreply */
    uint32_t xid = ((uint32_t)p[4] << 24) | ((uint32_t)p[5] << 16) |
                   ((uint32_t)p[6] << 8) | p[7];
    if (xid != dhcp_xid) return;
    uint32_t yiaddr = ((uint32_t)p[16] << 24) | ((uint32_t)p[17] << 16) |
                      ((uint32_t)p[18] << 8) | p[19];

    int msg = 0;
    uint32_t server = 0;
    int i = 240;
    while (i < (int)len) {
        uint8_t t = p[i];
        if (t == 255) break;
        if (t == 0) { i++; continue; }
        if (i + 1 >= (int)len) break;
        int l = p[i + 1];
        if (i + 2 + l > (int)len) break;
        const uint8_t* v = p + i + 2;
        if (t == 53 && l >= 1) msg = v[0];
        else if (t == 1 && l >= 4)
            dhcp_mask = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                        ((uint32_t)v[2] << 8) | v[3];
        else if (t == 3 && l >= 4)
            dhcp_router = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                          ((uint32_t)v[2] << 8) | v[3];
        else if (t == 6 && l >= 4)
            dhcp_dns = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                       ((uint32_t)v[2] << 8) | v[3];
        else if (t == 54 && l >= 4)
            server = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                     ((uint32_t)v[2] << 8) | v[3];
        i += 2 + l;
    }

    if (msg == 2 && yiaddr) {                    /* OFFER */
        dhcp_offer_ip = yiaddr;
        dhcp_offer_server = server ? server : yiaddr;
        dhcp_got_offer = 1;
    } else if (msg == 5) {                       /* ACK */
        dhcp_ack_ip = yiaddr ? yiaddr : dhcp_offer_ip;
        dhcp_got_ack = 1;
    } else if (msg == 6) {                       /* NAK */
        dhcp_got_nak = 1;
    }
}

int net_dhcp(char* err) {
    if (!g_nic_ok) {
        kstrcpy(err, g_sky2_found ?
                 "Marvell 88E8072 found; sky2 init failed" : "no supported network card");
        return -1;
    }
    if (active_nic == &sky2_ops && !sky2_wait_link(8000)) {
        sky2_debug_dump();
        sky2_debug_link();
        kstrcpy(err, "no link (check cable/switch)");
        return -1;
    }
    dhcp_xid = get_ticks() * 2654435761u ^ 0x5CA1AB1E;
    dhcp_got_offer = dhcp_got_ack = dhcp_got_nak = 0;
    dhcp_offer_ip = dhcp_offer_server = 0;
    dhcp_mask = dhcp_router = dhcp_dns = 0;

    uint32_t start = get_ticks(), interval = 1000;
    uint32_t last = start - interval;             /* transmit DISCOVER immediately */
    int sent_request = 0;
    while ((int32_t)(get_ticks() - start) < 20000) {
        if (dhcp_got_offer && !sent_request) {
            dhcp_send(3);
            sent_request = 1;
            last = get_ticks();
            interval = 1000;
        } else if ((int32_t)(get_ticks() - last) >= (int32_t)interval) {
            if (dhcp_got_offer && !dhcp_got_ack) dhcp_send(3);
            else if (!dhcp_got_offer) dhcp_send(1);
            last = get_ticks();
            if (interval < 4000) interval *= 2;
        }
        net_poll();
        net_idle_halt();
        if (dhcp_got_nak) { kstrcpy(err, "DHCP NAK from server"); return -1; }
        if (dhcp_got_ack) {
            g_ip = dhcp_ack_ip;
            g_mask = dhcp_mask ? dhcp_mask : 0xFFFFFF00u;
            g_gw = dhcp_router ? dhcp_router : ((g_ip & g_mask) | 1);
            g_dns = dhcp_dns ? dhcp_dns : g_gw;
            g_have_ip = 1;
            char s[20];
            net_ip_str(g_ip, s);
            serial_puts("[dhcp] leased "); serial_puts(s); serial_puts("\n");
            return 0;
        }
    }
    if (active_nic == &sky2_ops) {
        sky2_debug_dump();
        sky2_debug_link();
    }
    kstrcpy(err, "DHCP timeout (cable/server?)");
    return -1;
}

int net_set_static(uint32_t ip, uint32_t mask, uint32_t gw, uint32_t dns) {
    if (!g_nic_ok) return -1;
    g_ip = ip; g_mask = mask; g_gw = gw; g_dns = dns;
    g_have_ip = 1;
    return 0;
}

/* ---------------- DNS ---------------- */
static uint16_t dns_port = 0, dns_expect_id = 0;
static int      dns_got = 0;
static uint8_t  dns_buf[512];
static uint16_t dns_len = 0;
static uint16_t dns_next_port = 49152;

static uint32_t dns_skip_name(const uint8_t* b, uint32_t len, uint32_t pos) {
    int hops = 0;
    while (pos < len && hops++ < 64) {
        uint8_t c = b[pos];
        if (c == 0) return pos + 1;
        if ((c & 0xC0) == 0xC0) return pos + 2;   /* compression pointer */
        pos += 1 + c;
    }
    return len;
}

int net_resolve(const char* name, uint32_t* ip, char* err) {
    if (net_parse_ip(name, ip)) return 0;
    if (!g_have_ip) { kstrcpy(err, "no IP (run dhcp)"); return -1; }
    if (!g_dns) { kstrcpy(err, "no DNS server"); return -1; }

    /* build query */
    uint8_t q[300];
    uint16_t id = (uint16_t)(get_ticks() & 0xFFFF);
    memset(q, 0, 12);
    q[0] = (uint8_t)(id >> 8); q[1] = (uint8_t)(id & 0xFF);
    q[2] = 0x01;                                  /* RD */
    q[5] = 1;                                     /* QDCOUNT=1 */
    uint32_t pos = 12;
    const char* p = name;
    while (*p && pos < 280) {
        uint32_t l = 0;
        while (p[l] && p[l] != '.' && p[l] != '/') l++;
        if (l == 0) break;
        q[pos++] = (uint8_t)l;
        for (uint32_t i = 0; i < l && pos < 290; i++) q[pos++] = (uint8_t)p[i];
        p += l;
        if (*p == '.') p++;
    }
    q[pos++] = 0;
    q[pos++] = 0; q[pos++] = 1;                   /* type A */
    q[pos++] = 0; q[pos++] = 1;                   /* class IN */

    for (int attempt = 0; attempt < 2; attempt++) {
        dns_port = dns_next_port++;
        if (dns_next_port < 49152 || dns_next_port > 60999) dns_next_port = 49152;
        dns_expect_id = id;
        dns_got = 0;
        if (udp_send(g_dns, dns_port, 53, q, pos, g_ip, 0) < 0) {
            kstrcpy(err, "DNS send failed"); return -1;
        }
        uint32_t start = get_ticks();
        while (!dns_got && (int32_t)(get_ticks() - start) < 3000) {
            net_poll();
            net_idle_halt();
        }
        if (!dns_got) continue;

        if (dns_len < 12) { kstrcpy(err, "DNS: short reply"); return -1; }
        int rcode = dns_buf[3] & 0x0F;
        if (rcode) { kstrcpy(err, "DNS: server error"); return -1; }
        uint16_t ancount = (uint16_t)((dns_buf[6] << 8) | dns_buf[7]);
        uint16_t qdcount = (uint16_t)((dns_buf[4] << 8) | dns_buf[5]);
        uint32_t p2 = 12;
        for (int i = 0; i < qdcount && p2 < dns_len; i++) {
            p2 = dns_skip_name(dns_buf, dns_len, p2);
            p2 += 4;
        }
        for (int i = 0; i < ancount && p2 < dns_len; i++) {
            p2 = dns_skip_name(dns_buf, dns_len, p2);
            if (p2 + 10 > dns_len) break;
            uint16_t type = (uint16_t)((dns_buf[p2] << 8) | dns_buf[p2 + 1]);
            uint16_t cls  = (uint16_t)((dns_buf[p2 + 2] << 8) | dns_buf[p2 + 3]);
            uint16_t rdlen = (uint16_t)((dns_buf[p2 + 8] << 8) | dns_buf[p2 + 9]);
            p2 += 10;
            if (p2 + rdlen > dns_len) break;
            if (type == 1 && cls == 1 && rdlen == 4) {
                *ip = ((uint32_t)dns_buf[p2] << 24) | ((uint32_t)dns_buf[p2 + 1] << 16) |
                      ((uint32_t)dns_buf[p2 + 2] << 8) | dns_buf[p2 + 3];
                return 0;
            }
            p2 += rdlen;
        }
        kstrcpy(err, "DNS: name not found");
        return -1;
    }
    kstrcpy(err, "DNS timeout");
    return -1;
}

/* ---------------- ping ---------------- */
int net_ping(uint32_t ip, uint32_t* rtt_ms) {
    if (!g_have_ip) return -3;
    static uint16_t seq = 0;
    seq++;
    ping_id = 0x4B41;                             /* 'KA' */
    ping_seq = seq;
    ping_got = 0;
    if (icmp_send_echo(ip, ping_id, seq) < 0) return -2;
    uint32_t start = get_ticks();
    while (!ping_got && (int32_t)(get_ticks() - start) < 1000) {
        net_poll();
        net_idle_halt();
    }
    if (!ping_got) return -1;
    *rtt_ms = ping_tick - start;
    return 0;
}

/* ---------------- receive path ---------------- */
static void eth_input(const uint8_t* f, uint32_t len) {
    if (len < 14) return;
    uint16_t type = (uint16_t)((f[12] << 8) | f[13]);
    if (type == 0x0806) { arp_input(f, len); return; }
    if (type != 0x0800) return;

    const uint8_t* iph = f + 14;
    uint32_t ilen = len - 14;
    if (ilen < 20) return;
    if ((iph[0] >> 4) != 4) return;
    uint32_t ihl = (uint32_t)(iph[0] & 0xF) * 4;
    if (ihl < 20 || ihl > ilen) return;
    if (iph[6] & 0x3F || iph[7]) return;          /* fragments unsupported */
    uint16_t tot = (uint16_t)((iph[2] << 8) | iph[3]);
    if (tot > ilen) tot = (uint16_t)ilen;
    if (tot < ihl) return;
    if (cksum_fold(cksum_add(iph, ihl, 0)) != 0) return;   /* bad header checksum */

    uint32_t d = ((uint32_t)iph[16] << 24) | ((uint32_t)iph[17] << 16) |
                 ((uint32_t)iph[18] << 8) | iph[19];
    uint32_t s = ((uint32_t)iph[12] << 24) | ((uint32_t)iph[13] << 16) |
                 ((uint32_t)iph[14] << 8) | iph[15];

    if (g_have_ip) {
        if (d != g_ip && d != 0xFFFFFFFFu && d != (g_ip | ~g_mask)) return;
    } else {
        /* DHCP servers may unicast an OFFER to yiaddr despite the broadcast
         * flag. Before configuration, only UDP traffic is useful to us. */
        if (iph[9] != 17) return;
    }

    uint8_t proto = iph[9];
    const uint8_t* pl = iph + ihl;
    uint32_t plen = (uint32_t)(tot - ihl);
    if (proto == 1) {
        icmp_input(pl, plen, s, f + 6);
    } else if (proto == 17) {
        if (plen < 8) return;
        uint16_t sport = (uint16_t)((pl[0] << 8) | pl[1]);
        uint16_t dport = (uint16_t)((pl[2] << 8) | pl[3]);
        uint16_t ulen = (uint16_t)((pl[4] << 8) | pl[5]);
        if (ulen > plen) ulen = (uint16_t)plen;
        if (ulen < 8) return;
        const uint8_t* data = pl + 8;
        uint32_t dlen = (uint32_t)(ulen - 8);
        if (dport == 68) {
            dhcp_input(data, dlen);
        } else if (dport == dns_port && sport == 53 && !dns_got) {
            if (dlen >= 2 && data[0] == (uint8_t)(dns_expect_id >> 8) &&
                data[1] == (uint8_t)(dns_expect_id & 0xFF)) {
                if (dlen > sizeof dns_buf) dlen = sizeof dns_buf;
                memcpy(dns_buf, data, dlen);
                dns_len = (uint16_t)dlen;
                dns_got = 1;
            }
        }
    } else if (proto == 6) {
        tcp_input(f, 14 + tot);
    }
}

void net_poll(void) {
    if (!active_nic) return;
    uint8_t frame[1514];
    int guard = 0, n;
    while ((n = active_nic->receive(frame, sizeof frame)) > 0 && guard++ < 32)
        eth_input(frame, (uint32_t)n);
    tcp_poll();
}

void net_init(void) {
    g_ip = g_mask = g_gw = g_dns = 0;
    g_have_ip = 0;
    memset(g_mac, 0, 6);
    memset(arp_tab, 0, sizeof arp_tab);
    active_nic = 0;
    g_sky2_found = 0;
    g_sky2_init_error = 0;
    if (rtl8139_init() == 0) {
        active_nic = &rtl8139_ops;
        g_nic_ok = 1;
        memcpy(g_mac, active_nic->mac(), 6);
        serial_puts("[net] RTL8139 found, MAC ok\n");
    } else {
        g_nic_ok = 0;
        int sky2_rc = sky2_init();
        if (sky2_rc == 0) {
            active_nic = &sky2_ops;
            g_nic_ok = 1;
            g_sky2_found = 1;
            memcpy(g_mac, active_nic->mac(), 6);
            serial_puts("[net] Marvell sky2 packet driver started\n");
        } else if (g_sky2_found) {
            serial_puts("[net] Marvell sky2 not available/failed\n");
        } else {
            serial_puts("[net] no supported NIC found\n");
        }
    }
}

void net_shutdown(void) {
    if (active_nic == &rtl8139_ops)
        rtl8139_shutdown();
    else if (active_nic == &sky2_ops)
        sky2_shutdown();
    active_nic = 0;
    g_nic_ok = 0;
}
