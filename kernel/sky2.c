#include "net.h"

/*
 * Poll-driven Marvell Yukon-2 driver, initially scoped to the single-port
 * 88E8072 (PCI 11ab:436c) used by the ProBook 4515s.
 *
 * Yukon-2 uses DMA list elements rather than an I/O-port register interface.
 * The rings and packet buffers below are static so they are contiguous and
 * addressable by the 32-bit DMA engine in this non-paged kernel.
 */

#define SKY2_VENDOR       0x11AB
#define SKY2_DEVICE       0x436C

#define B0_CTST           0x0004
#define B0_IMSK           0x000C
#define B0_HWE_IMSK       0x0014
#define B0_POWER_CTRL     0x0007
#define B2_MAC_1          0x0100
#define B2_MAC_CFG        0x011A
#define B2_CHIP_ID        0x011B
#define B2_E_0            0x011C
#define B2_Y2_CLK_GATE    0x011D
#define B2_Y2_CLK_CTRL    0x0120
#define B2_TST_CTRL1      0x0158
#define B2_I2C_IRQ        0x0168
#define B2_TI_CTRL        0x0138
#define B28_DPT_CTRL      0x0E08
#define GMAC_TI_ST_CTRL   0x0E18
#define B28_Y2_ASF_STAT   0x0E68
#define HCU_CCSR_AHB_RST  (1u << 9)
#define HCU_CCSR_CPU_RST_MODE (1u << 8)
#define HCU_CCSR_UC_STATE_MSK 3u
#define Y2_HW_WOL_ON      (1u << 15)

#define B8_Q_REGS         0x0400
#define B16_RAM_REGS      0x0800
#define Y2_B8_PREF_REGS   0x0450
#define Q_R1              0x0000
#define Q_XA1             0x0280
#define Q_CSR             0x34
#define Q_WM              0x40
#define PREF_CTRL         0x00
#define PREF_LAST         0x04
#define PREF_ADDR_LO      0x08
#define PREF_ADDR_HI      0x0C
#define PREF_PUT          0x14
#define RB_START          0x00
#define RB_END            0x04
#define RB_WP             0x08
#define RB_RP             0x0C
#define RB_RX_UTHP        0x18
#define RB_RX_LTHP        0x1C
#define RB_RX_UTPP        0x10
#define RB_RX_LTPP        0x14
#define RB_CTRL           0x28

#define STAT_CTRL         0x0E80
#define STAT_LAST_IDX     0x0E84
#define STAT_LIST_LO      0x0E88
#define STAT_LIST_HI      0x0E8C
#define STAT_TX_IDX_TH    0x0E98
#define STAT_PUT_IDX      0x0E9C
#define STAT_FIFO_WM      0x0EAC
#define STAT_FIFO_ISR_WM  0x0EAD
#define STAT_TX_TIMER_INI 0x0EC0
#define STAT_ISR_TIMER_INI 0x0ED0
#define STAT_LEV_TIMER_INI 0x0EB0
#define STAT_TX_TIMER_CTRL 0x0EC8
#define STAT_ISR_TIMER_CTRL 0x0ED8
#define STAT_LEV_TIMER_CTRL 0x0EB8

#define GMAC_BASE         0x2800
#define GMAC_CTRL         0x0F00
#define GPHY_CTRL         0x0F04
#define GMAC_IRQ_SRC      0x0F08
#define GMAC_IRQ_MSK      0x0F0C
#define GM_GP_STAT        0x0000
#define GM_GP_CTRL        0x0004
#define GM_TX_CTRL        0x0008
#define GM_RX_CTRL        0x000C
#define GM_TX_FLOW_CTRL   0x0010
#define GM_TX_PARAM       0x0014
#define GM_SERIAL_MODE    0x0018
#define GM_SRC_ADDR_1L    0x001C
#define GM_SRC_ADDR_2L    0x0028
#define GM_MC_ADDR_H1     0x0034
#define GM_MC_ADDR_H2     0x0038
#define GM_MC_ADDR_H3     0x003C
#define GM_MC_ADDR_H4     0x0040
#define GM_SMI_CTRL       0x0080
#define GM_SMI_DATA       0x0084
#define GM_PHY_ADDR       0x0088
#define GM_MIB_BASE       0x0100
#define GM_RXF_UC_OK      (GM_MIB_BASE + 0)
#define GM_RXF_BC_OK      (GM_MIB_BASE + 8)
#define GM_RXF_MC_OK      (GM_MIB_BASE + 24)
#define GM_RXF_FCS_ERR    (GM_MIB_BASE + 32)
#define RX_GMF_CTRL_T     0x0C48
#define RX_GMF_FL_MSK     0x0C4C
#define RX_GMF_FL_THR     0x0C50
#define RX_GMF_TR_THR     0x0C54
#define TX_GMF_CTRL_T     0x0D48
#define TXA_CTRL          0x0210
#define GMAC_LINK_CTRL    0x0F10
#define B2_GP_IO          0x015C
#define B3_RI_WTO_R1      0x0190
#define B3_RI_WTO_XA1     0x0191
#define B3_RI_WTO_XS1     0x0192
#define B3_RI_RTO_R1      0x0193
#define B3_RI_RTO_XA1     0x0194
#define B3_RI_RTO_XS1     0x0195
#define RAM_RI_CTRL       0x01A0

#define Y2_CFG_SPC        0x1C00
#define PCI_DEV_REG1      0x40
#define PCI_DEV_REG3      0x80
#define PCI_DEV_REG4      0x84
#define PCI_DEV_REG5      0x88
#define PCI_CFG_REG1      0x94
#define PCI_ASPM_CONTROL_MSK ((1u << 15) | (1u << 14) | (1u << 13) | (1u << 12))
#define PCI_CTL_TIM_VMAIN_AV_MSK (3u << 27)

#define CS_MRST_CLR       (1u << 3)
#define CS_RST_CLR        (1u << 1)
#define CS_RST_SET        1u
#define Y2_ASF_DISABLE    (1u << 12)
#define Y2_ASF_RESET      (1u << 3)
#define DPT_STOP          1u
#define TIM_STOP          2u
#define TIM_CLR_IRQ       1u
#define SC_STAT_CLR_IRQ   (1u << 4)
#define SC_STAT_OP_ON     (1u << 3)
#define SC_STAT_RST_CLR   (1u << 1)
#define SC_STAT_RST_SET   1u

#define BMU_CLR_IRQ_PAR   (1u << 11)
#define BMU_CLR_IRQ_CHK   (1u << 10)
#define BMU_STOP          (1u << 9)
#define BMU_START         (1u << 8)
#define BMU_FIFO_OP_ON    (1u << 7)
#define BMU_FIFO_ENA      (1u << 5)
#define BMU_FIFO_RST      (1u << 4)
#define BMU_OP_ON         (1u << 3)
#define BMU_OP_OFF        (1u << 2)
#define BMU_RST_CLR       (1u << 1)
#define BMU_RST_SET       1u
#define BMU_CLR_RESET     (BMU_FIFO_RST | BMU_OP_OFF | BMU_RST_CLR)
#define BMU_OPER_INIT     (BMU_CLR_IRQ_PAR | BMU_CLR_IRQ_CHK | BMU_START | BMU_FIFO_ENA | BMU_OP_ON)
#define PREF_OP_ON        (1u << 3)
#define PREF_RST_CLR      (1u << 1)
#define PREF_RST_SET      1u
#define RB_RST_CLR        (1u << 1)
#define RB_ENA_STFWD      (1u << 5)
#define RB_ENA_OP_MD      (1u << 3)
#define RI_RST_CLR        (1u << 1)

#define GMC_RST_CLR       (1u << 1)
#define GMC_BYP_MACSECRX_ON (1u << 13)
#define GMC_BYP_MACSECTX_ON (1u << 11)
#define GMC_BYP_RETR_ON     (1u << 9)
#define GPC_RST_CLR       (1u << 1)
#define GPC_RST_SET       1u
#define GM_GPCR_TX_ENA    (1u << 12)
#define GM_GPCR_RX_ENA    (1u << 11)
#define GM_TXCR_CRC_DIS   (1u << 14)
#define GM_TXCR_PAD_DIS   (1u << 13)
#define GM_RXCR_UCF_ENA   (1u << 15)
#define GM_RXCR_MCF_ENA   (1u << 14)
#define GM_RXCR_CRC_DIS   (1u << 13)
#define GM_SMOD_VLAN_ENA  (1u << 9)
#define GM_IS_TX_FF_UR    (1u << 3)
#define GMF_RX_F_FL_ON    (1u << 7)
#define GMF_RX_OVER_ON    (1u << 19)
#define GMF_OPER_ON       (1u << 3)
#define GMF_RST_CLR       (1u << 1)
#define TX_STFW_ENA       (1u << 30)
#define TXA_ENA_ARB       (1u << 1)
#define GMLC_RST_SET      1u
#define GMLC_RST_CLR      (1u << 1)
#define RI_TIMEOUT_53     36u
#define GLB_GPIO_STAT_RACE_DIS (1u << 13)

#define GM_SMI_CT_PHY_AD(x) (((uint16_t)(x) << 11) & (0x1Fu << 11))
#define GM_SMI_CT_REG_AD(x) (((uint16_t)(x) << 6) & (0x1Fu << 6))
#define GM_SMI_CT_OP_RD    (1u << 5)
#define GM_SMI_CT_RD_VAL   (1u << 4)
#define GM_SMI_CT_BUSY     (1u << 3)
#define PHY_CTRL           0
#define PHY_STATUS         1
#define PHY_AN_ADVERT      4
#define PHY_1000_CTRL      9
#define PHY_EXT_ADDR       0x16
#define PHY_RESET          (1u << 15)
#define PHY_ANE            (1u << 12)
#define PHY_RESTART_AN     (1u << 9)
#define PHY_PDOWN          (1u << 11)
#define PHY_AN_ADV_ALL     0x0DE1
#define PHY_1000_ADV_ALL   0x0300

#define RX_LE_COUNT        128
#define TX_LE_COUNT        128
#define STATUS_LE_COUNT    512
#define RX_PENDING         19
#define RX_BUFFER_SIZE     1520
#define RX_BUFFER_COUNT    RX_PENDING
#define TX_BUFFER_COUNT    8
#define TX_PENDING_MAX     7
#define RX_QUEUE_DEPTH     8
#define RING_NEXT(i, n)   (((i) + 1) & ((n) - 1))

#define HW_OWNER           0x80
#define OP_ADDR64          0x21
#define OP_PACKET          0x41
#define OP_RXSTAT          0x60
#define OP_TXINDEXLE       0x68
#define OP_TCPSTART        0x12
#define EOP                0x80
#define GMR_FS_RX_OK       (1u << 8)
#define GMR_FS_ANY_ERR     0x0000187Bu

typedef struct __attribute__((packed)) {
    uint32_t addr;
    uint16_t length;
    uint8_t ctrl;
    uint8_t opcode;
} sky2_le_t;

typedef struct __attribute__((packed)) {
    uint32_t status;
    uint16_t length;
    uint8_t css;
    uint8_t opcode;
} sky2_status_le_t;

static volatile uint8_t* regs;
static uint8_t mac_addr[6];
static int started;
static uint8_t hw_chip_id;
static uint8_t hw_revision;
static uint8_t pci_bus, pci_slot;

static sky2_le_t tx_ring[TX_LE_COUNT] __attribute__((aligned(4096)));
static sky2_le_t rx_ring[RX_LE_COUNT] __attribute__((aligned(4096)));
static sky2_status_le_t status_ring[STATUS_LE_COUNT] __attribute__((aligned(4096)));
static uint8_t rx_buffers[RX_BUFFER_COUNT][RX_BUFFER_SIZE] __attribute__((aligned(64)));
static uint8_t tx_buffers[TX_BUFFER_COUNT][1536] __attribute__((aligned(64)));
static uint8_t rx_queue[RX_QUEUE_DEPTH][1514];
static uint16_t rx_queue_len[RX_QUEUE_DEPTH];
static unsigned rx_queue_head, rx_queue_tail, rx_queue_used;
static unsigned rx_put, rx_next, tx_put, tx_cons, tx_used, tx_buf_next, status_cons;

uint32_t g_sky2_status_les, g_sky2_rx_status, g_sky2_rx_ok;
uint32_t g_sky2_rx_bad, g_sky2_last_bad, g_sky2_tx_submit, g_sky2_tx_reports;
uint32_t g_sky2_diag_valid, g_sky2_diag_chip, g_sky2_diag_rev, g_sky2_diag_ram_kb;
uint32_t g_sky2_diag_gp_stat, g_sky2_diag_gp_ctrl, g_sky2_diag_phy11;
uint32_t g_sky2_diag_rx_uc, g_sky2_diag_rx_bc, g_sky2_diag_rx_mc, g_sky2_diag_rx_fcs;

static inline uint8_t rd8(uint32_t off) {
    return *(volatile uint8_t*)(regs + off);
}
static inline uint16_t rd16(uint32_t off) {
    return *(volatile uint16_t*)(regs + off);
}
static inline uint32_t rd32(uint32_t off) {
    return *(volatile uint32_t*)(regs + off);
}
static inline void wr8(uint32_t off, uint8_t v) {
    *(volatile uint8_t*)(regs + off) = v;
}
static inline void wr16(uint32_t off, uint16_t v) {
    *(volatile uint16_t*)(regs + off) = v;
}
static inline void wr32(uint32_t off, uint32_t v) {
    *(volatile uint32_t*)(regs + off) = v;
}

static inline uint32_t queue_reg(uint32_t q, uint32_t r) {
    return B8_Q_REGS + q + r;
}
static inline uint32_t prefetch_reg(uint32_t q, uint32_t r) {
    return Y2_B8_PREF_REGS + q + r;
}
static inline uint32_t ram_reg(uint32_t q, uint32_t r) {
    return B16_RAM_REGS + q + r;
}
static inline uint32_t gmac_reg(uint32_t p, uint32_t r) {
    return GMAC_BASE + p * 0x1000 + r;
}
static inline uint32_t port_reg(uint32_t p, uint32_t r) {
    return p * 0x80 + r;
}
static inline void dma_barrier(void) {
    asm volatile("" : : : "memory");
}

static void wait_short(void) {
    for (volatile unsigned i = 0; i < 10000; i++) { }
}

static int valid_mac(const uint8_t* m) {
    int any = 0;
    for (int i = 0; i < 6; i++) any |= m[i];
    return any && !(m[0] & 1);
}

static void serial_hex32(uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    char out[9];
    for (int i = 7; i >= 0; i--) {
        out[i] = hex[value & 0x0F];
        value >>= 4;
    }
    out[8] = 0;
    serial_puts(out);
}

static void gma_write16(unsigned port, uint32_t reg, uint16_t value) {
    wr16(gmac_reg(port, reg), value);
}
static uint16_t gma_read16(unsigned port, uint32_t reg) {
    return rd16(gmac_reg(port, reg));
}
static uint32_t gma_read32(unsigned port, uint32_t reg) {
    uint32_t base = gmac_reg(port, reg);
    return (uint32_t)rd16(base) | ((uint32_t)rd16(base + 4) << 16);
}

static int phy_write(uint16_t reg, uint16_t value) {
    gma_write16(0, GM_SMI_DATA, value);
    gma_write16(0, GM_SMI_CTRL, GM_SMI_CT_PHY_AD(0) | GM_SMI_CT_REG_AD(reg));
    for (unsigned i = 0; i < 10000; i++) {
        uint16_t c = gma_read16(0, GM_SMI_CTRL);
        if (c == 0xFFFF) return -1;
        if (!(c & GM_SMI_CT_BUSY)) return 0;
    }
    return -1;
}

static int phy_read(uint16_t reg, uint16_t* value) {
    gma_write16(0, GM_SMI_CTRL, GM_SMI_CT_PHY_AD(0) |
                GM_SMI_CT_REG_AD(reg) | GM_SMI_CT_OP_RD);
    for (unsigned i = 0; i < 10000; i++) {
        uint16_t c = gma_read16(0, GM_SMI_CTRL);
        if (c == 0xFFFF) return -1;
        if (c & GM_SMI_CT_RD_VAL) {
            *value = gma_read16(0, GM_SMI_DATA);
            return 0;
        }
    }
    return -1;
}

static void set_mac_registers(uint32_t reg) {
    gma_write16(0, reg, (uint16_t)mac_addr[0] | ((uint16_t)mac_addr[1] << 8));
    gma_write16(0, reg + 4, (uint16_t)mac_addr[2] | ((uint16_t)mac_addr[3] << 8));
    gma_write16(0, reg + 8, (uint16_t)mac_addr[4] | ((uint16_t)mac_addr[5] << 8));
}

static void ram_set(uint32_t q, uint32_t start_kb, uint32_t space_kb) {
    uint32_t start = start_kb * 128;
    uint32_t space = space_kb * 128;
    uint32_t base = ram_reg(q, 0);
    wr8(base + RB_CTRL, RB_RST_CLR);
    wr32(base + RB_START, start);
    wr32(base + RB_END, start + space - 1);
    wr32(base + RB_WP, start);
    wr32(base + RB_RP, start);
    if (q == Q_R1) {
        wr32(base + RB_RX_UTHP, space - space / 4);
        wr32(base + RB_RX_LTHP, space / 2);
        wr32(base + RB_RX_UTPP, space - 256);
        wr32(base + RB_RX_LTPP, space / 4);
    } else {
        wr8(base + RB_CTRL, RB_ENA_STFWD);
    }
    wr8(base + RB_CTRL, RB_ENA_OP_MD);
    (void)rd8(base + RB_CTRL);
}

static void queue_reset(uint32_t q) {
    uint32_t csr = queue_reg(q, Q_CSR);
    wr32(csr, BMU_CLR_RESET);
    wr32(csr, BMU_OPER_INIT);
    wr32(csr, BMU_FIFO_OP_ON);
    wr32(queue_reg(q, Q_WM), 0x600);
}

static void prefetch_init(uint32_t q, const void* ring, uint16_t last) {
    uint32_t base = prefetch_reg(q, 0);
    uint32_t addr = (uint32_t)ring;
    wr32(base + PREF_CTRL, PREF_RST_SET);
    wr32(base + PREF_CTRL, PREF_RST_CLR);
    wr32(base + PREF_ADDR_HI, 0);
    wr32(base + PREF_ADDR_LO, addr);
    wr16(base + PREF_LAST, last);
    wr32(base + PREF_CTRL, PREF_OP_ON);
    (void)rd32(base + PREF_CTRL);
}

static void publish_index(uint32_t q, uint16_t idx) {
    dma_barrier();
    wr16(prefetch_reg(q, PREF_PUT), idx);
}

static void add_rx_descriptor(unsigned buffer) {
    sky2_le_t* le = &rx_ring[rx_put];
    le->addr = (uint32_t)rx_buffers[buffer];
    le->length = RX_BUFFER_SIZE;
    le->ctrl = 0;
    dma_barrier();
    le->opcode = OP_PACKET | HW_OWNER;
    rx_put = RING_NEXT(rx_put, RX_LE_COUNT);
}

static void status_unit_init(void) {
    memset(status_ring, 0, sizeof status_ring);
    status_cons = 0;
    wr32(STAT_CTRL, SC_STAT_RST_SET);
    wr32(STAT_CTRL, SC_STAT_RST_CLR);
    wr32(STAT_LIST_LO, (uint32_t)status_ring);
    wr32(STAT_LIST_HI, 0);
    wr16(STAT_LAST_IDX, STATUS_LE_COUNT - 1);
    wr16(STAT_TX_IDX_TH, 10);
    wr8(STAT_FIFO_WM, 16);
    wr8(STAT_FIFO_ISR_WM, 16);
    /* Yukon II's internal clock is 125 MHz; these are 1ms/20us/100us. */
    wr32(STAT_TX_TIMER_INI, 125000);
    wr32(STAT_ISR_TIMER_INI, 2500);
    wr32(STAT_LEV_TIMER_INI, 12500);
    wr32(STAT_CTRL, SC_STAT_OP_ON);
    wr8(STAT_TX_TIMER_CTRL, 1u << 2);
    wr8(STAT_LEV_TIMER_CTRL, 1u << 2);
    wr8(STAT_ISR_TIMER_CTRL, 1u << 2);
}

static int core_reset_and_setup(void) {
    uint32_t cfg = Y2_CFG_SPC;
    uint32_t pci1;

    /* Let the device's PCI-side clock/reset logic run before touching queues. */
    wr32(cfg + PCI_DEV_REG3, 0);
    wr8(B0_CTST, CS_RST_CLR);
    hw_chip_id = rd8(B2_CHIP_ID);
    hw_revision = (rd8(B2_MAC_CFG) >> 4) & 0x0F;
    if (hw_chip_id < 0xB3 || hw_chip_id > 0xBA) return -2;

    if (hw_chip_id == 0xB5) {
        uint16_t hcu = rd16(B28_Y2_ASF_STAT);
        hcu &= ~(HCU_CCSR_AHB_RST | HCU_CCSR_CPU_RST_MODE |
                 HCU_CCSR_UC_STATE_MSK);
        wr16(B28_Y2_ASF_STAT, hcu);
    } else {
        wr8(B28_Y2_ASF_STAT, Y2_ASF_RESET);
    }
    wr16(B0_CTST, Y2_ASF_DISABLE);
    wr8(B0_CTST, CS_RST_SET);
    wr8(B0_CTST, CS_RST_CLR);
    wr8(B2_TST_CTRL1, 1u << 1); /* permit Yukon-specific PCI config writes */
    wr8(B0_CTST, CS_MRST_CLR);

    /* Power the controller from VCC and enable its clocks. */
    wr8(B0_POWER_CTRL, 0xA6); /* VCC on; switch control enabled */
    wr32(B2_Y2_CLK_CTRL, 1); /* disable core clock division */
    wr8(B2_Y2_CLK_GATE, 0);
    if (hw_chip_id == 0xB4 || hw_chip_id == 0xB5 || hw_chip_id == 0xB9) {
        wr32(cfg + PCI_DEV_REG3, 0);
        uint32_t pcie = rd32(cfg + PCI_DEV_REG4);
        pcie &= PCI_ASPM_CONTROL_MSK;
        wr32(cfg + PCI_DEV_REG4, pcie);
        pcie = rd32(cfg + PCI_DEV_REG5);
        pcie &= PCI_CTL_TIM_VMAIN_AV_MSK;
        wr32(cfg + PCI_DEV_REG5, pcie);
        wr32(cfg + PCI_CFG_REG1, 0);
        wr16(B0_CTST, Y2_HW_WOL_ON);
        pcie = rd32(B2_GP_IO) | GLB_GPIO_STAT_RACE_DIS;
        wr32(B2_GP_IO, pcie);
        (void)rd32(B2_GP_IO);
    }
    pci1 = rd32(cfg + PCI_DEV_REG1);
    if (pci1 != 0xFFFFFFFFu) {
        pci1 &= ~(1u << 26); /* PHY 1 power-down */
        wr32(cfg + PCI_DEV_REG1, pci1);
    }
    wr8(B2_TST_CTRL1, 1u); /* disable Yukon-specific PCI config writes */

    wr32(B0_IMSK, 0);
    wr32(B0_HWE_IMSK, 0);
    wr32(B2_I2C_IRQ, 1);
    wr8(B2_TI_CTRL, TIM_STOP);
    wr8(B2_TI_CTRL, TIM_CLR_IRQ);
    wr8(B28_DPT_CTRL, DPT_STOP);
    wr8(GMAC_TI_ST_CTRL, TIM_STOP);
    wr8(GMAC_TI_ST_CTRL, TIM_CLR_IRQ);
    wr8(TXA_CTRL, TXA_ENA_ARB);
    wr8(RAM_RI_CTRL, RI_RST_CLR);
    wr8(B3_RI_WTO_R1, RI_TIMEOUT_53);
    wr8(B3_RI_WTO_XA1, RI_TIMEOUT_53);
    wr8(B3_RI_WTO_XS1, RI_TIMEOUT_53);
    wr8(B3_RI_RTO_R1, RI_TIMEOUT_53);
    wr8(B3_RI_RTO_XA1, RI_TIMEOUT_53);
    wr8(B3_RI_RTO_XS1, RI_TIMEOUT_53);
    wr8(port_reg(0, GMAC_LINK_CTRL), GMLC_RST_SET);
    wr8(port_reg(0, GMAC_LINK_CTRL), GMLC_RST_CLR);
    (void)rd8(GMAC_IRQ_SRC);
    wr8(GMAC_IRQ_MSK, 0);

    /* Reset the sole active port and bring up the copper PHY. */
    wr8(port_reg(0, GPHY_CTRL), GPC_RST_SET);
    wr8(port_reg(0, GPHY_CTRL), GPC_RST_CLR);
    wr8(port_reg(0, GMAC_CTRL), GMC_RST_CLR);
    if (hw_chip_id == 0xB5 || hw_chip_id == 0xB9)
        wr16(port_reg(0, GMAC_CTRL), GMC_BYP_MACSECRX_ON |
             GMC_BYP_MACSECTX_ON | GMC_BYP_RETR_ON);
    wait_short();
    if (phy_write(PHY_CTRL, PHY_RESET) < 0) return -3;
    for (unsigned i = 0; i < 10000; i++) {
        uint16_t v;
        if (phy_read(PHY_CTRL, &v) == 0 && !(v & PHY_RESET)) break;
        if (i == 9999) return -3;
    }
    if (phy_write(PHY_AN_ADVERT, PHY_AN_ADV_ALL) < 0 ||
        phy_write(PHY_1000_CTRL, PHY_1000_ADV_ALL) < 0 ||
        phy_write(PHY_CTRL, PHY_ANE | PHY_RESTART_AN) < 0)
        return -3;

    /* Yukon EC-U/Ultra PHY analog front-end settings (page 255). */
    if (hw_chip_id == 0xB4 || hw_chip_id == 0xBA) {
        if (phy_write(PHY_EXT_ADDR, 255) == 0) {
            (void)phy_write(0x18, 0xAA99);
            (void)phy_write(0x17, 0x2011);
            if (hw_chip_id == 0xB4) {
                (void)phy_write(0x18, 0xA204);
                (void)phy_write(0x17, 0x2002);
            }
            (void)phy_write(PHY_EXT_ADDR, 0);
            (void)phy_write(PHY_CTRL, PHY_ANE | PHY_RESTART_AN);
        }
    }

    /* Prepare GMAC to accept our unicast and broadcast traffic. */
    (void)rd8(port_reg(0, GMAC_IRQ_SRC));
    wr8(port_reg(0, GMAC_IRQ_MSK), GM_IS_TX_FF_UR);
    gma_write16(0, GM_TX_CTRL, 0x1000); /* collision threshold 4; pad/CRC on */
    gma_write16(0, GM_RX_CTRL, GM_RXCR_UCF_ENA | GM_RXCR_MCF_ENA | GM_RXCR_CRC_DIS);
    gma_write16(0, GM_TX_FLOW_CTRL, 0xFFFF);
    gma_write16(0, GM_TX_PARAM, 0xD7C4);
    gma_write16(0, GM_SERIAL_MODE, (4u << 11) | GM_SMOD_VLAN_ENA | 0x1E);
    set_mac_registers(GM_SRC_ADDR_1L);
    set_mac_registers(GM_SRC_ADDR_2L);
    /* Accept broadcast/multicast traffic, including DHCP replies. */
    gma_write16(0, GM_MC_ADDR_H1, 0xFFFF);
    gma_write16(0, GM_MC_ADDR_H2, 0xFFFF);
    gma_write16(0, GM_MC_ADDR_H3, 0xFFFF);
    gma_write16(0, GM_MC_ADDR_H4, 0xFFFF);
    gma_write16(0, GM_PHY_ADDR, 1u << 5);
    for (uint32_t i = GM_MIB_BASE; i <= 0x025C; i += 4) (void)gma_read16(0, i);
    gma_write16(0, GM_PHY_ADDR, 0);

    wr8(port_reg(0, RX_GMF_CTRL_T), GMF_RST_CLR);
    uint32_t rx_gmf = GMF_OPER_ON | GMF_RX_F_FL_ON;
    if (hw_chip_id == 0xB5 || hw_chip_id == 0xB8)
        rx_gmf |= GMF_RX_OVER_ON;
    wr32(port_reg(0, RX_GMF_CTRL_T), rx_gmf);
    wr16(port_reg(0, RX_GMF_FL_MSK), 0x187B);
    wr16(port_reg(0, RX_GMF_FL_THR), 0x0B);
    wr8(port_reg(0, TX_GMF_CTRL_T), GMF_RST_CLR);
    wr16(port_reg(0, TX_GMF_CTRL_T), GMF_OPER_ON);
    wr32(port_reg(0, TX_GMF_CTRL_T), TX_STFW_ENA);

    /* Allocate on-chip packet RAM between receive and transmit queues. */
    uint32_t ram_kb = (uint32_t)rd8(B2_E_0) * 4;
    if (ram_kb) {
        uint32_t rx_kb = ram_kb < 16 ? ram_kb / 2 : 8 + 2 * (ram_kb - 16) / 3;
        if (rx_kb >= ram_kb) rx_kb = ram_kb / 2;
        ram_set(Q_R1, 0, rx_kb);
        ram_set(Q_XA1, rx_kb, ram_kb - rx_kb);
    }

    queue_reset(Q_XA1);
    queue_reset(Q_R1);
    memset(tx_ring, 0, sizeof tx_ring);
    memset(rx_ring, 0, sizeof rx_ring);
    memset(rx_buffers, 0, sizeof rx_buffers);
    tx_put = tx_cons = 1; /* entry zero carries the initial 64-bit address */
    tx_ring[0].addr = 0;
    tx_ring[0].opcode = OP_ADDR64 | HW_OWNER;
    tx_used = tx_buf_next = 0;

    prefetch_init(Q_XA1, tx_ring, TX_LE_COUNT - 1);
    prefetch_init(Q_R1, rx_ring, RX_LE_COUNT - 1);
    rx_put = rx_next = 0;

    /* Older Yukon-2 list engines require this checksum-disable list element. */
    if (hw_chip_id != 0xB5 && hw_chip_id != 0xB8 && hw_chip_id != 0xB9) {
        sky2_le_t* le = &rx_ring[rx_put++];
        le->addr = (14u << 16) | 14u;
        le->length = 0;
        le->ctrl = 0;
        le->opcode = OP_TCPSTART | HW_OWNER;
        wr32(queue_reg(Q_R1, Q_CSR), 1u << 12); /* disable RX checksum */
    }
    for (unsigned i = 0; i < RX_PENDING; i++) add_rx_descriptor(i);
    wr16(port_reg(0, RX_GMF_TR_THR), (RX_BUFFER_SIZE - 8) / 4);
    wr32(port_reg(0, RX_GMF_CTRL_T), 1u << 27); /* truncate at descriptor length */
    publish_index(Q_R1, (uint16_t)rx_put);

    status_unit_init();
    rx_queue_head = rx_queue_tail = rx_queue_used = 0;

    /* Enable the MAC after autonegotiation has been started. */
    gma_write16(0, GM_GP_CTRL, GM_GPCR_TX_ENA | GM_GPCR_RX_ENA);
    wr32(B0_IMSK, 0x0000001D); /* port 1 MAC/PHY/queue status */
    started = 1;
    return 0;
}

static void complete_tx(uint32_t done) {
    done &= 0x0FFF;
    if (done >= TX_LE_COUNT) return;
    unsigned guard = 0;
    while (tx_cons != done && tx_used && guard++ < TX_PENDING_MAX) {
        tx_cons = RING_NEXT(tx_cons, TX_LE_COUNT);
        tx_used--;
    }
}

static void enqueue_rx(const uint8_t* packet, uint16_t len) {
    if (!len || len > 1514 || rx_queue_used == RX_QUEUE_DEPTH) return;
    memcpy(rx_queue[rx_queue_head], packet, len);
    rx_queue_len[rx_queue_head] = len;
    rx_queue_head = (rx_queue_head + 1) % RX_QUEUE_DEPTH;
    rx_queue_used++;
}

/* Drain the hardware completion queue; valid frames are copied out before
 * their DMA buffers are returned to the receive ring. */
static void poll_status(void) {
    uint16_t put = rd16(STAT_PUT_IDX) & (STATUS_LE_COUNT - 1);
    unsigned count = 0;
    while (status_cons != put && count++ < STATUS_LE_COUNT) {
        sky2_status_le_t* le = &status_ring[status_cons];
        uint8_t opcode = le->opcode;
        if (!(opcode & HW_OWNER)) break;
        uint32_t status = le->status;
        uint16_t len = le->length;
        le->opcode = 0;
        status_cons = RING_NEXT(status_cons, STATUS_LE_COUNT);
        g_sky2_status_les++;

        switch (opcode & ~HW_OWNER) {
        case OP_RXSTAT: {
            g_sky2_rx_status++;
            unsigned slot = rx_next;
            rx_next = (rx_next + 1) % RX_BUFFER_COUNT;
            if ((status & GMR_FS_RX_OK) && !(status & GMR_FS_ANY_ERR) &&
                len <= RX_BUFFER_SIZE) {
                g_sky2_rx_ok++;
                enqueue_rx(rx_buffers[slot], len);
            } else {
                g_sky2_rx_bad++;
                g_sky2_last_bad = status;
            }
            add_rx_descriptor(slot);
            publish_index(Q_R1, (uint16_t)rx_put);
            break;
        }
        case OP_TXINDEXLE:
            g_sky2_tx_reports++;
            complete_tx(status);
            break;
        default:
            break;
        }
    }
    wr32(STAT_CTRL, SC_STAT_CLR_IRQ);
}

int sky2_init(void) {
    uint8_t bus = 0, slot = 0;
    g_sky2_found = 0;
    g_sky2_init_error = 0;
    started = 0;
    g_sky2_status_les = g_sky2_rx_status = g_sky2_rx_ok = 0;
    g_sky2_rx_bad = g_sky2_last_bad = g_sky2_tx_submit = g_sky2_tx_reports = 0;

    if (pci_find_id(SKY2_VENDOR, SKY2_DEVICE, &bus, &slot) < 0) return -1;
    g_sky2_found = 1;
    pci_bus = bus;
    pci_slot = slot;

    uint32_t bar0 = pci_cfg_read(bus, slot, 0, 0x10);
    uint32_t bar1 = pci_cfg_read(bus, slot, 0, 0x14);
    uint32_t bar_type = (bar0 >> 1) & 3;
    uint32_t mmio;
    if (bar0 & 1) {
        g_sky2_init_error = 1; /* sky2 requires MMIO, not an I/O BAR */
        serial_puts("[sky2] BAR0 is I/O, expected MMIO\n");
        return -1;
    }
    if (bar_type == 3 || (bar_type == 2 && bar1 != 0)) {
        g_sky2_init_error = 1; /* reserved type or address above 4 GiB */
        serial_puts("[sky2] unusable 64-bit/reserved BAR0 low=");
        serial_hex32(bar0);
        serial_puts(" high=");
        serial_hex32(bar1);
        serial_puts("\n");
        return -1;
    }
    if (bar_type == 1)
        mmio = bar0 & 0x000FFFF0u; /* 20-bit memory BAR */
    else
        mmio = bar0 & 0xFFFFFFF0u; /* 32-bit or 64-bit with upper dword zero */
    if (!mmio) {
        g_sky2_init_error = 1; /* unassigned BAR */
        serial_puts("[sky2] BAR0 is not assigned a physical address\n");
        return -1;
    }
    regs = (volatile uint8_t*)mmio;

    uint32_t command = pci_cfg_read(bus, slot, 0, 0x04);
    pci_cfg_write(bus, slot, 0, 0x04, command | 0x00000006u); /* MMIO + bus master */
    if (rd32(B0_CTST) == 0xFFFFFFFFu || rd8(B2_CHIP_ID) == 0xFF) {
        g_sky2_init_error = 2; /* BAR is not responding */
        return -1;
    }
    for (int i = 0; i < 6; i++) mac_addr[i] = rd8(B2_MAC_1 + (uint32_t)i);
    if (!valid_mac(mac_addr)) {
        g_sky2_init_error = 3; /* invalid factory address */
        return -1;
    }
    if (core_reset_and_setup() < 0) {
        g_sky2_init_error = 4; /* unsupported chip or PHY access/reset */
        return -1;
    }
    serial_puts("[sky2] 88E8072 Yukon-II DMA rings initialized\n");
    return 0;
}

int sky2_send(const uint8_t* frame, uint32_t len) {
    if (!started || !frame || len < 14 || len > 1514) return -1;
    poll_status();
    uint32_t wait_start = get_ticks();
    unsigned guard = 0;
    while (tx_used >= TX_PENDING_MAX &&
           (uint32_t)(get_ticks() - wait_start) < 20 && guard++ < 100000)
        poll_status();
    if (tx_used >= TX_PENDING_MAX) return -1;

    unsigned idx = tx_put;
    unsigned buf = tx_buf_next;
    tx_buf_next = (tx_buf_next + 1) % TX_BUFFER_COUNT;
    memcpy(tx_buffers[buf], frame, len);
    sky2_le_t* le = &tx_ring[idx];
    le->addr = (uint32_t)tx_buffers[buf];
    le->length = (uint16_t)len;
    le->ctrl = EOP;
    dma_barrier();
    le->opcode = OP_PACKET | HW_OWNER;
    tx_put = RING_NEXT(tx_put, TX_LE_COUNT);
    tx_used++;
    g_sky2_tx_submit++;
    publish_index(Q_XA1, (uint16_t)tx_put);
    return (int)len;
}

int sky2_receive(uint8_t* frame, uint32_t max) {
    if (!started || !frame || !max) return 0;
    poll_status();
    if (!rx_queue_used) return 0;
    uint16_t len = rx_queue_len[rx_queue_tail];
    if (len > max) len = (uint16_t)max;
    memcpy(frame, rx_queue[rx_queue_tail], len);
    rx_queue_tail = (rx_queue_tail + 1) % RX_QUEUE_DEPTH;
    rx_queue_used--;
    return (int)len;
}

uint8_t* sky2_mac(void) {
    return mac_addr;
}

int sky2_link_up(void) {
    if (!started) return 0;
    return (gma_read16(0, GM_GP_STAT) & (1u << 12)) != 0;
}

void sky2_shutdown(void) {
    if (!started) return;
    wr32(B0_IMSK, 0);
    wr32(B0_HWE_IMSK, 0);
    wr8(port_reg(0, GMAC_IRQ_MSK), 0);

    uint16_t gp = gma_read16(0, GM_GP_CTRL);
    gp &= (uint16_t)~(GM_GPCR_TX_ENA | GM_GPCR_RX_ENA);
    gma_write16(0, GM_GP_CTRL, gp);
    wr32(queue_reg(Q_R1, Q_CSR), BMU_STOP);
    wr32(queue_reg(Q_XA1, Q_CSR), BMU_STOP);
    wr32(STAT_CTRL, SC_STAT_RST_SET);
    wr32(prefetch_reg(Q_R1, PREF_CTRL), PREF_RST_SET);
    wr32(prefetch_reg(Q_XA1, PREF_CTRL), PREF_RST_SET);

    /* No more DMA after this point. */
    uint32_t command = pci_cfg_read(pci_bus, pci_slot, 0, 0x04);
    pci_cfg_write(pci_bus, pci_slot, 0, 0x04, command & ~0x00000004u);
    started = 0;
}

int sky2_wait_link(uint32_t timeout_ms) {
    if (!started) return 0;
    uint32_t start = get_ticks();
    do {
        uint16_t ps;
        if (phy_read(0x11, &ps) == 0 &&
            (ps & (1u << 10)) && (ps & (1u << 11)))
            return 1;
        /* Hardware auto-update owns MAC speed/duplex. The GP_STAT link bit
         * may settle before the PHY's resolved bit becomes readable. */
        if (sky2_link_up()) return 1;
    } while ((uint32_t)(get_ticks() - start) < timeout_ms);
    return 0;
}

void sky2_debug_dump(void) {
    uint16_t phy1 = 0xFFFF, phy11 = 0xFFFF;
    (void)phy_read(PHY_STATUS, &phy1);
    (void)phy_read(0x11, &phy11);
    serial_puts("[sky2] GP_STAT="); serial_hex32(gma_read16(0, GM_GP_STAT));
    serial_puts(" GP_CTRL="); serial_hex32(gma_read16(0, GM_GP_CTRL));
    serial_puts(" PHY1="); serial_hex32(phy1);
    serial_puts(" PHY11="); serial_hex32(phy11);
    serial_puts("\n[sky2] stat_put="); serial_hex32(rd16(STAT_PUT_IDX));
    serial_puts(" stat_cons="); serial_hex32(status_cons);
    serial_puts(" status_les="); serial_hex32(g_sky2_status_les);
    serial_puts(" rx_status="); serial_hex32(g_sky2_rx_status);
    serial_puts(" rx_ok="); serial_hex32(g_sky2_rx_ok);
    serial_puts(" rx_bad="); serial_hex32(g_sky2_rx_bad);
    serial_puts(" last_bad="); serial_hex32(g_sky2_last_bad);
    serial_puts(" tx="); serial_hex32(g_sky2_tx_submit);
    serial_puts(" tx_reports="); serial_hex32(g_sky2_tx_reports);
    serial_puts(" tx_used="); serial_hex32(tx_used);
    serial_puts("\n");
}

void sky2_debug_link(void) {
    uint16_t phy11 = 0xFFFF;
    (void)phy_read(0x11, &phy11);

    g_sky2_diag_chip = hw_chip_id;
    g_sky2_diag_rev = hw_revision;
    g_sky2_diag_ram_kb = (uint32_t)rd8(B2_E_0) * 4;
    g_sky2_diag_gp_stat = gma_read16(0, GM_GP_STAT);
    g_sky2_diag_gp_ctrl = gma_read16(0, GM_GP_CTRL);
    g_sky2_diag_phy11 = phy11;
    g_sky2_diag_rx_uc = gma_read32(0, GM_RXF_UC_OK);
    g_sky2_diag_rx_bc = gma_read32(0, GM_RXF_BC_OK);
    g_sky2_diag_rx_mc = gma_read32(0, GM_RXF_MC_OK);
    g_sky2_diag_rx_fcs = gma_read32(0, GM_RXF_FCS_ERR);
    g_sky2_diag_valid = 1;

    serial_puts("[sky2] chip="); serial_hex32(g_sky2_diag_chip);
    serial_puts(" rev="); serial_hex32(g_sky2_diag_rev);
    serial_puts(" ram_kb="); serial_hex32(g_sky2_diag_ram_kb);
    serial_puts(" GP_STAT="); serial_hex32(g_sky2_diag_gp_stat);
    serial_puts(" GP_CTRL="); serial_hex32(g_sky2_diag_gp_ctrl);
    serial_puts(" PHY11="); serial_hex32(g_sky2_diag_phy11);
    serial_puts("\n[sky2] MIB rx_uc="); serial_hex32(g_sky2_diag_rx_uc);
    serial_puts(" rx_bc="); serial_hex32(g_sky2_diag_rx_bc);
    serial_puts(" rx_mc="); serial_hex32(g_sky2_diag_rx_mc);
    serial_puts(" fcs_err="); serial_hex32(g_sky2_diag_rx_fcs);
    serial_puts("\n");
}
