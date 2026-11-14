#include "net.h"

/* RTL8139 (and compatible) driver, poll-driven (IMR=0).
 * RX: 8K ring + 2048 pad, RxNoWrap mode - packets that cross the ring
 * end are written linearly into the pad, so reads are plain memcpy.
 * Register facts cross-checked against QEMU's rtl8139 model and Linux 8139too. */

#define R_MAC      0x00
#define R_MAR      0x08
#define R_TSD0     0x10
#define R_TSAD0    0x20
#define R_RBSTART  0x30
#define R_CR       0x37
#define R_CAPR     0x38
#define R_IMR      0x3C
#define R_ISR      0x3E
#define R_RCR      0x44
#define R_CONFIG1  0x52

#define CR_RST     0x10
#define CR_RE      0x08
#define CR_TE      0x04
#define CR_BUFE    0x01

#define RX_RING    8192
#define RX_PAD     2048
#define TSD_OWN    0x2000     /* host owns descriptor = free */

static uint16_t io_base;
static int      present = 0;
static uint8_t  mac[6];
static uint8_t  rx_buf[RX_RING + RX_PAD] __attribute__((aligned(16)));
static uint8_t  tx_buf[4][1536] __attribute__((aligned(16)));
static uint32_t cur_rx;
static int      tx_cur;

static void iowait(int n) { while (n--) io_wait(); }

int rtl8139_init(void) {
    present = 0;
    uint8_t bus, slot;
    uint16_t io;
    if (pci_find(0x10EC, 0x8139, &bus, &slot, &io) < 0) return -1;
    io_base = io;

    /* wake: clear power management enable in CONFIG1 */
    outb(io_base + R_CONFIG1, inb(io_base + R_CONFIG1) & ~0x08);

    /* soft reset, bounded wait */
    outb(io_base + R_CR, CR_RST);
    int t = 200000;
    while ((inb(io_base + R_CR) & CR_RST) && --t) iowait(10);
    if (!t) { serial_puts("[rtl8139] reset timeout\n"); return -2; }

    for (int i = 0; i < 6; i++) mac[i] = inb(io_base + R_MAC + i);
    for (int i = 0; i < 8; i++) outb(io_base + R_MAR + i, 0xFF);  /* accept all multicast */

    outl(io_base + R_RBSTART, (uint32_t)rx_buf);
    for (int i = 0; i < 4; i++)
        outl(io_base + R_TSAD0 + i * 4, (uint32_t)tx_buf[i]);

    cur_rx = 0;
    tx_cur = 0;
    outw(io_base + R_CAPR, 0xFFF0);                 /* read ptr = -16 */

    /* RCR: RxNoWrap | accept broadcast/multicast/ours | DMA unlimited | FIFO none */
    outl(io_base + R_RCR, 0x80 | 0x0E | (7u << 8) | (7u << 13));

    outw(io_base + R_IMR, 0x0000);                  /* no interrupts, we poll */
    outb(io_base + R_CR, CR_TE | CR_RE);
    outw(io_base + R_ISR, 0xFFFF);                  /* ack anything pending */

    present = 1;
    return 0;
}

uint8_t* rtl8139_mac(void) { return mac; }

void rtl8139_shutdown(void) {
    if (!present) return;
    outw(io_base + R_IMR, 0);
    outb(io_base + R_CR, 0); /* stop receive and transmit engines */
    present = 0;
}

int rtl8139_send(const uint8_t* frame, uint32_t len) {
    if (!present) return -1;
    if (len > 1514) return -1;
    uint32_t wire_len = len < 60 ? 60 : len;         /* min ethernet frame */

    int i = tx_cur;
    uint32_t start = get_ticks();
    /* descriptor free when TSD OWN bit set (bounded wait) */
    while (!(inw(io_base + R_TSD0 + i * 4) & TSD_OWN) &&
           (int32_t)(get_ticks() - start) < 100) { }

    memcpy(tx_buf[i], frame, len);
    if (wire_len > len) memset(tx_buf[i] + len, 0, wire_len - len);
    outl(io_base + R_TSAD0 + i * 4, (uint32_t)tx_buf[i]);
    outl(io_base + R_TSD0 + i * 4, wire_len | 0x03000000u);  /* size | IFG96 */
    tx_cur = (i + 1) & 3;
    return (int)wire_len;
}

int rtl8139_receive(uint8_t* buf, uint32_t max) {
    if (!present) return 0;
    if (inb(io_base + R_CR) & CR_BUFE) return 0;      /* ring empty */

    uint32_t off = cur_rx & (RX_RING - 1);
    uint32_t hdr = *(volatile uint32_t*)(rx_buf + off);
    uint16_t status = (uint16_t)(hdr & 0xFFFF);
    uint16_t size = (uint16_t)(hdr >> 16);

    if (!(status & 0x0001) || size < 8 || size > 1526) {
        /* lost sync in the ring - re-init the chip */
        serial_puts("[rtl8139] rx resync\n");
        rtl8139_init();
        return 0;
    }

    uint32_t pkt_len = (uint32_t)size - 4;           /* strip CRC */
    if (pkt_len > max) pkt_len = max;
    memcpy(buf, rx_buf + off + 4, pkt_len);          /* may extend into pad - contiguous */

    cur_rx = (cur_rx + size + 4 + 3) & ~3u;
    outw(io_base + R_CAPR, (uint16_t)(cur_rx - 16));
    return (int)pkt_len;
}
