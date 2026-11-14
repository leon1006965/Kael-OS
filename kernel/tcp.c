#include "net.h"

/* Minimal single-connection blocking TCP. One connection at a time is
 * plenty for a text browser. Retransmission keyed off the 1000Hz tick. */

#define T_CLOSED   0
#define T_SYNSENT  1
#define T_ESTAB    2

#define RX_CAP     8192

typedef struct {
    int      state;
    uint32_t rip;
    uint16_t rport, lport;
    uint32_t isn, snd_nxt, rcv_nxt;
    /* last unacked segment (for retransmit) */
    uint8_t  u_data[1500];
    uint32_t u_len, u_seq, u_end;
    uint8_t  u_flags;
    int      u_active;
    uint32_t u_tick;
    int      retries;
    /* receive ring */
    uint8_t  rx[RX_CAP];
    uint32_t rx_h, rx_t;
    int      eof, err;
} conn_t;

static conn_t c;
static uint16_t next_port = 49152;

static void tcp_hex32(uint32_t v) {
    static const char hex[] = "0123456789ABCDEF";
    char out[9];
    for (int i = 7; i >= 0; i--) {
        out[i] = hex[v & 0xF];
        v >>= 4;
    }
    out[8] = 0;
    serial_puts(out);
}

static uint32_t rx_used(void) { return (c.rx_h + RX_CAP - c.rx_t) & (RX_CAP - 1); }
static uint32_t rx_free(void) { return (RX_CAP - 1) - rx_used(); }

static void tcp_xmit(uint8_t flags, uint32_t seq, const uint8_t* data, uint32_t dlen) {
    if (!g_have_ip) return;
    uint8_t seg[20 + 1500];
    uint8_t* t = seg;
    t[0] = (uint8_t)(c.lport >> 8); t[1] = (uint8_t)(c.lport & 0xFF);
    t[2] = (uint8_t)(c.rport >> 8); t[3] = (uint8_t)(c.rport & 0xFF);
    t[4] = (uint8_t)(seq >> 24); t[5] = (uint8_t)(seq >> 16);
    t[6] = (uint8_t)(seq >> 8);  t[7] = (uint8_t)(seq);
    t[8] = (uint8_t)(c.rcv_nxt >> 24); t[9] = (uint8_t)(c.rcv_nxt >> 16);
    t[10] = (uint8_t)(c.rcv_nxt >> 8); t[11] = (uint8_t)(c.rcv_nxt);
    t[12] = 5 << 4;                       /* data offset = 20 bytes, no options */
    t[13] = flags;
    uint16_t win = (uint16_t)rx_free();
    t[14] = (uint8_t)(win >> 8); t[15] = (uint8_t)(win & 0xFF);
    t[16] = 0; t[17] = 0;                 /* checksum */
    t[18] = 0; t[19] = 0;                 /* urgent */
    if (dlen) memcpy(seg + 20, data, dlen);

    /* pseudo header + segment checksum */
    uint8_t ph[12 + 20 + 1500];
    uint32_t s = htonl(g_ip), d = htonl(c.rip);
    memcpy(ph, &s, 4);
    memcpy(ph + 4, &d, 4);
    ph[8] = 0; ph[9] = 6;
    uint16_t tl = (uint16_t)(20 + dlen);
    ph[10] = (uint8_t)(tl >> 8); ph[11] = (uint8_t)(tl & 0xFF);
    memcpy(ph + 12, seg, 20 + dlen);
    uint16_t ck = cksum_fold(cksum_add(ph, 12 + 20 + dlen, 0));
    if (ck == 0) ck = 0xFFFF;
    t[16] = (uint8_t)(ck >> 8); t[17] = (uint8_t)(ck & 0xFF);

    ip_send_raw(c.rip, 6, seg, 20 + dlen);
}

static void save_unacked(uint8_t flags, uint32_t seq, const uint8_t* data,
                         uint32_t dlen, uint32_t extra) {
    if (dlen) memcpy(c.u_data, data, dlen);
    c.u_flags = flags;
    c.u_seq = seq;
    c.u_len = dlen;
    c.u_end = seq + dlen + extra;
    c.u_active = 1;
    c.u_tick = get_ticks();
    c.retries = 0;
}

void tcp_poll(void) {
    if (!c.u_active || c.err) return;
    if ((int32_t)(get_ticks() - c.u_tick) < 500) return;    /* RTO 500ms */
    if (++c.retries > 8) { c.err = 2; c.state = T_CLOSED; c.u_active = 0; return; }
    c.u_tick = get_ticks();
    tcp_xmit(c.u_flags, c.u_seq, c.u_len ? c.u_data : 0, c.u_len);
}

int tcp_connect(uint32_t ip, uint16_t port, char* err) {
    if (!g_nic_ok) {
        kstrcpy(err, g_sky2_found ?
                 "Marvell 88E8072 found; sky2 init failed" : "no supported network card");
        return -1;
    }
    if (!g_have_ip) { kstrcpy(err, "no IP (run dhcp)"); return -1; }

    memset(&c, 0, sizeof c);
    c.state = T_SYNSENT;
    c.rip = ip;
    c.rport = port;
    c.lport = next_port++;
    if (next_port < 49152 || next_port > 60999) next_port = 49152;
    c.isn = get_ticks() ^ 0x12345678u;
    c.snd_nxt = c.isn + 1;

    save_unacked(0x02, c.isn, 0, 0, 1);           /* SYN, consumes 1 seq */
    serial_puts("[tcp] SYN sent\n");
    tcp_xmit(0x02, c.isn, 0, 0);

    uint32_t start = get_ticks();
    while (c.state == T_SYNSENT && !c.err &&
           (int32_t)(get_ticks() - start) < 5000) {
        net_poll();
        net_idle_halt();
    }

    if (c.state != T_ESTAB) {
        kstrcpy(err, c.err == 1 ? "peer reset during connect" :
                     c.err == 2 ? "connect retransmit timeout" : "connect timeout");
        c.state = T_CLOSED;
        return -1;
    }
    serial_puts("[tcp] connected\n");
    return 0;
}

int tcp_send(const void* data, uint32_t len, char* err) {
    if (c.state != T_ESTAB) { kstrcpy(err, "not connected"); return -1; }
    const uint8_t* p = (const uint8_t*)data;
    while (len) {
        uint32_t n = len > 1460 ? 1460 : len;
        save_unacked(0x18, c.snd_nxt, p, n, 0);   /* PSH|ACK */
        tcp_xmit(0x18, c.snd_nxt, p, n);
        c.snd_nxt += n;

        uint32_t start = get_ticks();
        while (c.u_active && !c.err &&
               (int32_t)(get_ticks() - start) < 5000) {
            net_poll();
            net_idle_halt();
        }
        if (c.err) {
            kstrcpy(err, c.err == 1 ? "peer reset during send" : "send retransmit timeout");
            return -1;
        }
        if (c.u_active) { kstrcpy(err, "send timeout"); return -1; }
        p += n; len -= n;
    }
    return 0;
}

int tcp_recv(void* buf, uint32_t max, uint32_t timeout_ticks, char* err) {
    uint32_t start = get_ticks();
    for (;;) {
        uint32_t used = rx_used();
        if (used) {
            uint32_t n = used < max ? used : max;
            uint32_t h = c.rx_t & (RX_CAP - 1);
            uint32_t first = RX_CAP - h;
            if (first > n) first = n;
            memcpy(buf, c.rx + h, first);
            memcpy((uint8_t*)buf + first, c.rx, n - first);
            c.rx_t += n;
            /* Advertise the receive space the application just freed. */
            tcp_xmit(0x10, c.snd_nxt, 0, 0);
            return (int)n;
        }
        if (c.err) {
            kstrcpy(err, c.err == 1 ? "peer reset during receive" : "receive retransmit timeout");
            return -1;
        }
        if (c.eof) return 0;
        if (c.state != T_ESTAB) { kstrcpy(err, "connection closed"); return -1; }
        if ((int32_t)(get_ticks() - start) >= (int32_t)timeout_ticks) {
            kstrcpy(err, "recv timeout"); return -1;
        }
        net_poll();
        net_idle_halt();
    }
}

void tcp_close(void) {
    if (c.state == T_ESTAB) {
        save_unacked(0x11, c.snd_nxt, 0, 0, 1);   /* FIN|ACK */
        tcp_xmit(0x11, c.snd_nxt, 0, 0);
        c.snd_nxt++;
        uint32_t start = get_ticks();
        while (c.u_active && !c.err &&
               (int32_t)(get_ticks() - start) < 1000) {
            net_poll();
            net_idle_halt();
        }
    }
    c.state = T_CLOSED;
    c.u_active = 0;
}

void tcp_input(const uint8_t* f, uint32_t len) {
    if (c.state == T_CLOSED && !c.u_active) return;
    if (len < 14 + 20) return;
    const uint8_t* iph = f + 14;
    uint32_t ihl = (uint32_t)(iph[0] & 0xF) * 4;
    if (len < 14 + ihl + 20) return;
    uint16_t tot = (uint16_t)((iph[2] << 8) | iph[3]);
    if (tot < ihl + 20) return;
    const uint8_t* t = iph + ihl;
    uint32_t tlen = (uint32_t)(tot - ihl);

    uint16_t sport = (uint16_t)((t[0] << 8) | t[1]);
    uint16_t dport = (uint16_t)((t[2] << 8) | t[3]);
    if (dport != c.lport || sport != c.rport) return;
    uint32_t s_ip = ((uint32_t)iph[12] << 24) | ((uint32_t)iph[13] << 16) |
                    ((uint32_t)iph[14] << 8) | iph[15];
    if (s_ip != c.rip) return;

    /* verify tcp checksum */
    uint8_t ph[12];
    uint32_t s = htonl(s_ip), d = htonl(g_ip);
    memcpy(ph, &s, 4); memcpy(ph + 4, &d, 4);
    ph[8] = 0; ph[9] = 6;
    ph[10] = (uint8_t)(tlen >> 8); ph[11] = (uint8_t)(tlen & 0xFF);
    uint32_t sum = cksum_add(ph, 12, 0);
    sum = cksum_add(t, tlen, sum);
    if (cksum_fold(sum) != 0) { serial_puts("[tcp] bad checksum\n"); return; }

    uint32_t seq = ((uint32_t)t[4] << 24) | ((uint32_t)t[5] << 16) |
                   ((uint32_t)t[6] << 8) | t[7];
    uint32_t ack = ((uint32_t)t[8] << 24) | ((uint32_t)t[9] << 16) |
                   ((uint32_t)t[10] << 8) | t[11];
    uint8_t flags = t[13];
    uint32_t doff = (uint32_t)(t[12] >> 4) * 4;
    if (doff < 20 || doff > tlen) return;
    const uint8_t* data = t + doff;
    uint32_t dlen = tlen - doff;

    if (flags & 0x04) {                          /* RST */
        serial_puts("[tcp] RST state="); tcp_hex32((uint32_t)c.state);
        serial_puts(" seq="); tcp_hex32(seq);
        serial_puts(" ack="); tcp_hex32(ack);
        serial_puts(" expected_seq="); tcp_hex32(c.rcv_nxt);
        serial_puts(" expected_ack="); tcp_hex32(c.snd_nxt);
        serial_puts("\n");
        c.err = 1; c.state = T_CLOSED; c.u_active = 0;
        return;
    }

    if (c.state == T_SYNSENT) {
        if ((flags & 0x12) == 0x12 && ack == c.isn + 1) {
            serial_puts("[tcp] SYN-ACK received\n");
            c.rcv_nxt = seq + 1;
            c.u_active = 0;
            c.state = T_ESTAB;
            tcp_xmit(0x10, c.snd_nxt, 0, 0);     /* ACK */
        }
        return;
    }
    if (c.state != T_ESTAB) return;

    if (flags & 0x10 && c.u_active) {            /* ACK of our outstanding segment */
        if ((int32_t)(ack - c.u_end) >= 0 &&
            (int32_t)(ack - c.u_seq) > 0 &&
            (int32_t)(ack - c.snd_nxt) <= 0) {
            c.u_active = 0;
            c.retries = 0;
        }
    }

    if (dlen && seq == c.rcv_nxt) {
        uint32_t free = rx_free();
        uint32_t n = dlen < free ? dlen : free;
        if (n) {
            uint32_t h = c.rx_h & (RX_CAP - 1);
            uint32_t first = RX_CAP - h;
            if (first > n) first = n;
            memcpy(c.rx + h, data, first);
            memcpy(c.rx, data + first, n - first);
            c.rx_h += n;
            c.rcv_nxt += n;
        }
        tcp_xmit(0x10, c.snd_nxt, 0, 0);
    } else if (dlen) {
        tcp_xmit(0x10, c.snd_nxt, 0, 0);         /* dup/ooo: re-ACK */
    }

    if ((flags & 0x01) && seq + dlen == c.rcv_nxt) {   /* FIN */
        c.rcv_nxt++;
        c.eof = 1;
        tcp_xmit(0x10, c.snd_nxt, 0, 0);
    }
}
