/* Only built when the board asks for networking. This file lives in cores/,
 * so without the guard it would compile for every sketch -- including ones
 * built without lwIP on the include path. */
#ifdef CH32H4_ETHERNET

#include <errno.h>

/* The name this interface gives DHCP. Not const: eth_set_hostname() replaces
 * the pointer, and lwIP only stores it. */
const char *ch32h4_eth_hostname = "ch32h417";
/* Ethernet MAC and on-chip 100M PHY, bridged to lwip.
 *
 * The MAC is a Synopsys DWMAC -- the same IP STM32F4/F7 carry -- so the
 * register names, the 4-word chained descriptors and the overall shape of this
 * driver match ports/stm32/eth.c. What follows documents only where the CH32
 * differs, because those are the parts no STM32 reference will tell you.
 *
 * WCH's own driver for this part is EVT/EXAM/ETH/NetLib/eth_driver_100M.c,
 * paired with a precompiled libwchnet.a. The library is lwip with WCH's socket
 * API on top; MicroPython already vendors lwip, so only the hardware knowledge
 * below is taken from it. See docs/superpowers/specs/2026-08-12-ch32-ethernet-design.md.
 *
 * Four CH32-specific facts drive this file:
 *
 * 1. The PHY is ON-CHIP. Its MDI pins go straight to the RJ45 and reference
 *    manual table 31-1 lists them as "No IO configuration required" -- there is
 *    no RMII pin muxing to do, unlike every STM32 board. It answers on SMI
 *    address 1.
 * 2. It needs its own PLL: RCC_CTLR bit 26 ETH_PLLON, ready on bit 27.
 * 3. It is held in reset and powered down out of cold boot, by MACPHYCR
 *    bits 31 and 30.
 * 4. There is an undocumented analog trim that must be reapplied on every
 *    link-up. See eth_phy_para_cfg().
 */

#include <string.h>

#include "Arduino.h"
#include "ch32h4_fault.h"
#include "ch32h4_irq.h"

#include "ch32h417.h"
#include "ch32h4_eth.h"


#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "netif/ethernet.h"


/* On-chip PHY's SMI address. Fixed in silicon, not a board choice. */
#define PHY_ADDRESS (1)

/* MACPHYCR. Both of these are CH32 extensions to the DWMAC register file and
 * appear in no STM32 documentation. Out of reset the on-chip PHY is powered
 * down AND held in reset, so both must be touched before SMI reads return
 * anything but 0xffff. */
#define MACPHYCR_PHY_POWERDOWN (1u << 30)  // 1 = powered down
#define MACPHYCR_PHY_RESET_N   (1u << 31)  // 1 = out of reset

/* RCC_CTLR. The Ethernet block runs from its own 500 MHz PLL. */
#define RCC_CTLR_ETH_PLLON  (1u << 26)
#define RCC_CTLR_ETH_PLLRDY (1u << 27)

/* RCC_HBPCENR bit 14: Ethernet MAC clock. */
#define RCC_HBPCENR_ETHMACEN (1u << 14)

/* Descriptor counts. WCH ship four of each.
 *
 * Receive gets more, because frames are not processed in the interrupt (see
 * eth_rx_process) -- the descriptors have to hold whatever arrives between two
 * visits to the background hook. At 100 Mbit a millisecond is 12.5 KB, about
 * eight maximum-size frames, so twelve leaves headroom for a hook that is late.
 * Transmit needs no such margin: eth_netif_output waits for a free descriptor
 * rather than deferring. */
#define ETH_RX_DESC_NUM (12)
#define ETH_TX_DESC_NUM (4)

/* PHY registers beyond the IEEE-standard set. This PHY is paged: PHY_PAG_SEL
 * selects which bank registers 0x10-0x1e refer to. Forgetting to set the page
 * back to 0 makes later standard-register reads return another page's data,
 * which is why every paged access here restores it. */
/* PHY_PAG_SEL, PHY_INTERRUPT_MASK and PHY_INTERRUPT_IND come from
 * ch32h417_eth.h; only what that header lacks is defined here. */
#define PHY_PAGE_0          (0x00)
#define PHY_PAGE_7          (0x07)
#define PHY_STATUS_REG      (0x1a)  // page 0: live speed/duplex
#define PHY_STATUS_100M     (1u << 3)
#define PHY_STATUS_FULLDUP  (1u << 1)
#define PHY_INTMASK_LINK_IE (1u << 13)
#define PHY_INTMASK_LED_ON  (1u << 9)

/* Factory MAC address, six bytes stored HIGH byte first and read downwards.
 * WCHNET_GetMacAddr() walks backwards from +5, so the natural forward read
 * gives the address reversed -- a detail worth stating because getting it
 * wrong produces a MAC that looks plausible and is not the one on the label. */
#define ROM_CFG_USERADR_ID (0x1ffff7e8)

typedef struct _eth_t {
    struct netif netif;
    bool active;
    bool link_up;
    volatile bool link_event;  // set by the ISR, consumed by eth_poll()
    volatile bool rx_pending;  // set by the ISR, consumed by eth_rx_process()
    eth_stats_t stats;
} eth_t;

eth_t eth_instance;

/* Descriptors and frame buffers. These live in .ethram, in the shared SRAM
 * region, because the Ethernet DMA -- like the USB DMA before it, which is why
 * .usbram exists -- cannot reach DTCM, where .bss and the GC heap are. Putting
 * them in .bss produces a MAC that initialises cleanly, reports link up, and
 * silently never receives a frame.
 *
 * .ethram is NOLOAD and outside .bss, so startup's _sbss.._ebss clear does not
 * cover it; eth_init() zeroes it explicitly. */
#define ETH_RAM_ATTR __attribute__((section(".ethram"), aligned(32)))

static ETH_DMADESCTypeDef eth_rx_desc[ETH_RX_DESC_NUM] ETH_RAM_ATTR;
static ETH_DMADESCTypeDef eth_tx_desc[ETH_TX_DESC_NUM] ETH_RAM_ATTR;
static uint8_t eth_rx_buf[ETH_RX_DESC_NUM][ETH_MAX_PACKET_SIZE] ETH_RAM_ATTR;
static uint8_t eth_tx_buf[ETH_TX_DESC_NUM][ETH_MAX_PACKET_SIZE] ETH_RAM_ATTR;

static ETH_DMADESCTypeDef *eth_rx_cur;
static ETH_DMADESCTypeDef *eth_tx_cur;

extern uint8_t _sethram, _eethram;

/******************************************************************************/
// Serialising lwip against the receive interrupt

/* The ETH interrupt calls into lwip (netif->input), and so does the periodic
 * timer sweep. Something has to keep those two out of each other's data
 * structures, because lwip in NO_SYS mode does no locking of its own.
 *
 * This masks ONLY the Ethernet interrupt, not all of them. A blanket
 * interrupt lock would also stall the UART, whose receive path has a single
 * hardware byte of slack at 115200 -- so protecting the network stack would
 * corrupt the console. Masking the one interrupt that can re-enter lwip is
 * both sufficient and free of that side effect.
 *
 * Used ONLY around code that cannot raise. See mpconfigport.h for why this is
 * deliberately not wired to LWIP_ENTER. */
static uint32_t eth_lock_depth;

void eth_lwip_lock(void) {
    if (eth_lock_depth++ == 0) {
        NVIC_DisableIRQ(ETH_IRQn);
    }
}

void eth_lwip_unlock(void) {
    if (eth_lock_depth > 0 && --eth_lock_depth == 0) {
        NVIC_EnableIRQ(ETH_IRQn);
    }
}

/******************************************************************************/
// PHY access

uint16_t eth_phy_read(uint16_t reg) {
    return ETH_ReadPHYRegister(PHY_ADDRESS, reg);
}

void eth_phy_write(uint16_t reg, uint16_t val) {
    ETH_WritePHYRegister(PHY_ADDRESS, reg, val);
}

static uint16_t eth_phy_read_paged(uint16_t page, uint16_t reg) {
    eth_phy_write(PHY_PAG_SEL, page);
    uint16_t val = eth_phy_read(reg);
    eth_phy_write(PHY_PAG_SEL, PHY_PAGE_0);
    return val;
}

static void eth_phy_set_paged(uint16_t page, uint16_t reg, uint16_t bits) {
    eth_phy_write(PHY_PAG_SEL, page);
    eth_phy_write(reg, eth_phy_read(reg) | bits);
    eth_phy_write(PHY_PAG_SEL, PHY_PAGE_0);
}

/* Undocumented analog trim for the on-chip PHY, lifted verbatim from WCH's
 * PHY_PARA_CFG() macro in eth_driver.h. The address is inside the ETH block
 * (ETH_BASE is 0x40028000) but appears in no register map, and the triple
 * write of the same value is theirs, not a transcription error -- it reads
 * like a shift-register load that needs clocking through.
 *
 * WCH reapply it on every link-up rather than once at init. Doing the same,
 * because a trim that only mattered at init would not have been written that
 * way, and the cost is four stores. */
static void eth_phy_para_cfg(void) {
    volatile uint32_t *reg = (volatile uint32_t *)0x4002a00c;
    *reg = 0xfffff;
    *reg = 0xfffff;
    *reg = 0xfffff;
    *reg = 0xc2000;
}

/******************************************************************************/
// MAC bring-up

static void eth_get_mac_addr(uint8_t *out) {
    /* Stored high byte first, read downwards -- see ROM_CFG_USERADR_ID. */
    const uint8_t *src = (const uint8_t *)(ROM_CFG_USERADR_ID + 5);
    for (int i = 0; i < 6; i++) {
        out[i] = *src--;
    }
}

/* Drive the two Ethernet LEDs from the PHY rather than from software.
 *
 * Board wiring: ELED1 -> PF0 (green, link), ELED0 -> PF2 (yellow, activity),
 * each through a 0 ohm link. Both are AF10. Letting the PHY drive them gets
 * link and activity semantics -- including green being low-active -- for free
 * and costs no CPU time, which software blinking could not match. */
static void eth_led_init(void) {
    GPIO_InitTypeDef cfg;

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO | RCC_HB2Periph_GPIOF, ENABLE);
    (void)RCC->HB2PCENR;  // RMW enable; read back or the first AF write is lost

    GPIO_PinAFConfig(GPIOF, GPIO_PinSource0, GPIO_AF10);
    GPIO_PinAFConfig(GPIOF, GPIO_PinSource2, GPIO_AF10);

    cfg.GPIO_Speed = GPIO_Speed_Very_High;
    cfg.GPIO_Mode = GPIO_Mode_AF_PP;
    cfg.GPIO_Pin = GPIO_Pin_0;
    GPIO_Init(GPIOF, &cfg);
    cfg.GPIO_Pin = GPIO_Pin_2;
    GPIO_Init(GPIOF, &cfg);

    // Enable the PHY's own LED driver.
    eth_phy_set_paged(PHY_PAGE_7, PHY_INTERRUPT_MASK, PHY_INTMASK_LED_ON);
}

static int eth_mac_init(eth_t *self) {
    // 1. Ethernet PLL.
    RCC->CTLR |= RCC_CTLR_ETH_PLLON;
    uint32_t deadline = millis() + 100;
    while ((RCC->CTLR & RCC_CTLR_ETH_PLLRDY) == 0) {
        if (millis() > deadline) {
            return -ETIMEDOUT;
        }
    }

    // 2. MAC clock. Read back: the enable is a read-modify-write, and without
    // this the first ETH register access is dropped.
    RCC->HBPCENR |= RCC_HBPCENR_ETHMACEN;
    (void)RCC->HBPCENR;

    // 3. Release the on-chip PHY: power it up, then take it out of reset.
    ETH->MACPHYCR &= ~MACPHYCR_PHY_POWERDOWN;
    ETH->MACPHYCR |= MACPHYCR_PHY_RESET_N;
    delay(1);

    // 4. Software reset of the MAC and DMA. Self-clearing.
    ETH_SoftwareReset();
    deadline = millis() + 100;
    while (ETH->DMABMR & ETH_DMABMR_SR) {
        if (millis() > deadline) {
            return -ETIMEDOUT;
        }
    }

    /* SMI clock divider. Must be set before any PHY access, and it is the
     * reason step 4 comes first: the software reset clears MACMIIAR. */
    ETH->MACMIIAR = ETH_MACMIIAR_CR_Div42;

    /* MAC configuration. Speed and duplex are deliberately absent here --
     * they are not known until auto-negotiation finishes, and eth_link_up()
     * writes them.
     *
     * Unlike WCH's driver this does NOT set promiscuous mode or receive-all.
     * They ship it that way because their stack filters in software; we have a
     * hardware perfect filter and a MAC address, so let the MAC drop what is
     * not ours instead of spending interrupt time on every frame on the
     * segment. Broadcast stays on, or ARP and DHCP would never arrive. */
    ETH->MACCR = ETH_Watchdog_Enable | ETH_Jabber_Enable
        | ETH_InterFrameGap_96Bit | ETH_ChecksumOffload_Disable
        | ETH_AutomaticPadCRCStrip_Disable | ETH_LoopbackMode_Disable;

    ETH->MACFFR = ETH_ReceiveAll_Disable | ETH_PromiscuousMode_Disable
        | ETH_BroadcastFramesReception_Enable
        | ETH_MulticastFramesFilter_Perfect
        | ETH_UnicastFramesFilter_Perfect
        | ETH_PassControlFrames_BlockAll
        | ETH_DestinationAddrFilter_Normal
        | ETH_SourceAddrFilter_Disable;

    ETH->MACHTHR = 0;
    ETH->MACHTLR = 0;
    ETH->MACFCR = ETH_UnicastPauseFrameDetect_Disable
        | ETH_ReceiveFlowControl_Disable | ETH_TransmitFlowControl_Disable;
    ETH->MACVLANTR = ETH_VLANTagComparison_16Bit;

    ETH->DMAOMR = ETH_DropTCPIPChecksumErrorFrame_Enable
        | ETH_TransmitStoreForward_Enable
        | ETH_ForwardErrorFrames_Disable
        | ETH_ForwardUndersizedGoodFrames_Disable;

    // 5. Programme our MAC address into the perfect filter.
    const uint8_t *mac = self->netif.hwaddr;
    ETH->MACA0HR = (uint32_t)((mac[5] << 8) | mac[4]);
    ETH->MACA0LR = (uint32_t)(mac[0] | (mac[1] << 8) | (mac[2] << 16) | (mac[3] << 24));

    /* Mask the MMC counter interrupts. These fire when a statistics counter
     * reaches half full and are latched until the counter is read; left
     * unmasked they eventually assert forever and the ETH interrupt never
     * stops firing. */
    ETH->MMCTIMR = ETH_MMCTIMR_TGFM;
    ETH->MMCRIMR = ETH_MMCRIMR_RGUFM | ETH_MMCRIMR_RFCEM;

    // 6. Descriptors.
    ETH_DMATxDescChainInit(eth_tx_desc, &eth_tx_buf[0][0], ETH_TX_DESC_NUM);
    ETH_DMARxDescChainInit(eth_rx_desc, &eth_rx_buf[0][0], ETH_RX_DESC_NUM);
    __asm volatile("fence" ::: "memory");
    eth_rx_cur = eth_rx_desc;
    eth_tx_cur = eth_tx_desc;

    /* 7. Interrupts. PHYSR is the CH32 addition: the on-chip PHY reports link
     * changes through the DMA status register, so there is no MDIO poll loop
     * here of the sort an external PHY would need. */
    ETH_DMAITConfig(ETH_DMA_IT_NIS | ETH_DMA_IT_AIS | ETH_DMA_IT_R
        | ETH_DMA_IT_T | ETH_DMA_IT_RBU | ETH_DMA_IT_PHYSR, ENABLE);

    // Ask the PHY to interrupt on link change, and clear anything latched.
    eth_phy_set_paged(PHY_PAGE_7, PHY_INTERRUPT_MASK, PHY_INTMASK_LINK_IE);
    (void)eth_phy_read(PHY_INTERRUPT_IND);

    return 0;
}

/******************************************************************************/
// Link handling

bool eth_link_is_up(eth_t *self) {
    return self->link_up;
}

/* Apply the negotiated speed and duplex to the MAC, then start it.
 *
 * Read from the PHY's vendor status register rather than deriving it from the
 * advertised/link-partner registers: the vendor register reports what the link
 * actually settled on, which is what the MAC has to match. */
static void eth_link_up(eth_t *self) {
    eth_phy_para_cfg();

    uint16_t status = eth_phy_read_paged(PHY_PAGE_0, PHY_STATUS_REG);

    uint32_t maccr = ETH->MACCR & ~(ETH_Speed_1000M | ETH_Mode_FullDuplex);
    maccr |= (status & PHY_STATUS_100M) ? ETH_Speed_100M : ETH_Speed_10M;
    if (status & PHY_STATUS_FULLDUP) {
        maccr |= ETH_Mode_FullDuplex;
    }
    ETH->MACCR = maccr;

    ETH_Start();

    self->link_up = true;
    self->stats.link_changes++;
    netif_set_link_up(&self->netif);
}

static void eth_link_down(eth_t *self) {
    self->link_up = false;
    self->stats.link_changes++;
    netif_set_link_down(&self->netif);

    /* Drop an address that DHCP gave us. Keeping it across a cable pull would
     * leave the interface claiming a lease that the server is free to hand to
     * someone else, so on reconnect we would be a duplicate. A statically
     * configured address is left alone -- the user set that deliberately. */
    struct netif *netif = &self->netif;
    if (netif_dhcp_data(netif) != NULL) {
        if (dhcp_supplied_address(netif)) {
            ip4_addr_set_zero(&netif->ip_addr);
        }
        dhcp_stop(netif);
    }
}

/* Start DHCP if the interface is up and has no address yet.
 *
 * This runs on every link-up rather than only from eth_start(), and that is
 * the whole point: after a reset, auto-negotiation is still in progress when
 * active(True) returns, so a DHCP attempt made there has no link to send on.
 * Starting it only at active(True) meant a board that had just been reset
 * never acquired an address, while one whose link was already up did -- which
 * is exactly as confusing to debug as it sounds. */
static void eth_dhcp_start_if_needed(eth_t *self) {
    struct netif *netif = &self->netif;
    if (!netif_is_up(netif) || !ip4_addr_isany_val(*netif_ip4_addr(netif))) {
        return;
    }
    /* Only start when DHCP is not already running. dhcp_start() is idempotent,
     * but a stop-then-start every poll cycle reset the state machine (and the
     * DISCOVER backoff) once a second, so a lease could never complete. */
    if (netif_dhcp_data(netif) == NULL) {
        dhcp_start(netif);
    }
}

/* Reconcile our idea of the link with the PHY's.
 *
 * Deliberately NOT called from the interrupt. It does half a dozen MDIO reads,
 * each of which is 64 bits clocked at HCLK/42 -- about 30 us apiece -- and on
 * link-up it goes on to allocate inside lwip via DHCP. Neither belongs in an
 * ISR, so the handler only sets link_event and this runs from eth_poll(). */
static void eth_link_poll(eth_t *self) {
    /* Read BSR twice. IEEE 802.3 defines the link status bit as latching low:
     * a single read reports "down" if the link dropped at any point since the
     * last read, even if it is up right now. Reading once makes a cable
     * replug look like a permanently dead link. */
    (void)eth_phy_read(PHY_BSR);
    uint16_t bsr = eth_phy_read(PHY_BSR);
    uint16_t bcr = eth_phy_read(PHY_BCR);

    bool up;
    if (!(bsr & PHY_Linked_Status)) {
        up = false;
    } else if (bcr & PHY_AutoNegotiation) {
        /* Link is electrically up but the MAC must not start until
         * auto-negotiation has settled, or it runs at the wrong duplex. The
         * PHY raises another interrupt when negotiation completes. */
        up = (bsr & PHY_AutoNego_Complete) != 0;
    } else {
        up = true;
    }

    if (up != self->link_up) {
        if (up) {
            eth_link_up(self);
        } else {
            eth_link_down(self);
        }
    }

    if (up && self->active) {
        eth_dhcp_start_if_needed(self);
    }

    // Clear the PHY's latched interrupt, or it never asserts again.
    eth_phy_write(PHY_PAG_SEL, PHY_PAGE_0);
    (void)eth_phy_read(PHY_INTERRUPT_IND);
}

/* Called from the port's background hook. Two jobs: pick up link changes the
 * interrupt flagged, and re-check the link periodically anyway.
 *
 * The periodic re-check is not redundant. Auto-negotiation completing does not
 * reliably raise a second PHY interrupt on this part, so a board reset with a
 * cable already attached can otherwise sit link-down forever, having missed
 * the one edge it was waiting for. One MDIO read per second costs nothing. */
void eth_poll(void) {
    eth_t *self = &eth_instance;
    if (self->netif.input == NULL) {
        return;  // never initialised
    }

    static uint32_t next_check_ms;
    uint32_t now = millis();
    bool due = (int32_t)(now - next_check_ms) >= 0;

    if (!self->link_event && !due) {
        return;
    }
    self->link_event = false;
    next_check_ms = now + 1000;

    eth_link_poll(self);
}

/******************************************************************************/
// Receive

static void eth_rx_frame(eth_t *self, const uint8_t *buf, size_t len) {
    struct netif *netif = &self->netif;

    /* Record which frame we are about to hand to lwIP, so a lockup anywhere in
     * the receive path still leaves the frame type readable by the V3F after
     * the reset. EtherType at bytes 12..13; for ARP the opcode at 20..21, for
     * IP the protocol at byte 23. */
    uint16_t ethertype = (len >= 14) ? (uint16_t)((buf[12] << 8) | buf[13]) : 0;
    uint8_t detail = 0;
    if (ethertype == 0x0806 && len >= 22) {
        detail = (uint8_t)((buf[20] << 8) | buf[21]);  /* 1=request 2=reply */
    } else if (ethertype == 0x0800 && len >= 24) {
        detail = buf[23];
    }
    ch32h4_eth_phase = 1;
    ch32h4_eth_last_frame = ((uint32_t)ethertype << 16)
                          | ((uint32_t)detail << 8)
                          | (len & 0xFFu);

    struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    if (p == NULL) {
        self->stats.rx_dropped++;
        return;
    }

    /* Copy out of the DMA buffer rather than handing lwip a pointer into it.
     * The buffer has to stay in the shared region the DMA can reach, while
     * pbufs live in the fast DTCM heap; see the design doc for why that trade
     * is worth one memcpy at 259 MB/s against a 12.5 MB/s wire. */
    pbuf_take(p, buf, len);

    self->stats.rx_frames++;

    if (netif->input(p, netif) != ERR_OK) {
        pbuf_free(p);
        self->stats.rx_dropped++;
    }
}

/* Drain every descriptor the DMA has finished with.
 *
 * Runs in normal context from eth_rx_process(), NOT in the interrupt handler,
 * and that is the single most important structural decision in this driver.
 * netif->input() runs lwip's whole receive path -- ethernet_input, ip4_input,
 * tcp_input, and any callback the socket layer has registered -- which is a
 * deep call chain. Executing it on the interrupt stack, on top of whatever
 * depth the interrupted Python code had already reached, overflowed the stack
 * under sustained TCP load here: the board wedged mid-transfer and then reset
 * with no fault message, because the fault handler had no stack either.
 *
 * ports/stm32 does call input from its ISR and gets away with it. This port
 * has a 16 KB stack shared with the VM and no separate interrupt stack, so it
 * does not.
 *
 * The loop condition is OWN being clear, not the count of interrupts: one
 * receive interrupt can cover several frames, and stopping after one leaves
 * the rest sitting until the next frame arrives to nudge us. */
static void eth_rx_drain(eth_t *self) {
    while ((eth_rx_cur->Status & ETH_DMARxDesc_OWN) == 0) {
        uint32_t status = eth_rx_cur->Status;

        bool complete = (status & (ETH_DMARxDesc_FS | ETH_DMARxDesc_LS))
            == (ETH_DMARxDesc_FS | ETH_DMARxDesc_LS);

        if (complete && !(status & ETH_DMARxDesc_ES)) {
            /* Frame length includes the 4-byte CRC, which lwip must not see.
             * AutomaticPadCRCStrip is disabled, so it really is present. */
            size_t len = ((status & ETH_DMARxDesc_FL) >> 16);
            if (len >= 4 && len <= ETH_MAX_PACKET_SIZE) {
                eth_rx_frame(self, (const uint8_t *)eth_rx_cur->Buffer1Addr, len - 4);
            } else {
                self->stats.rx_dropped++;
            }
        } else {
            self->stats.rx_dropped++;
        }

        // Hand the descriptor back before moving on.
        eth_rx_cur->Status = ETH_DMARxDesc_OWN;
        __asm volatile("fence" ::: "memory");
        eth_rx_cur = (ETH_DMADESCTypeDef *)eth_rx_cur->Buffer2NextDescAddr;
        ch32h4_eth_phase = 4;
    }

    /* Demand-poll unconditionally. If the DMA suspended for want of a
     * descriptor while we were away, handing the descriptors back is not
     * enough to restart it -- it stays suspended until this register is
     * written, and the interface would go permanently deaf. */
    ETH->DMARPDR = 0;
}

/* Called from the background hook. Cheap when there is nothing to do: one read
 * of a flag the interrupt sets.
 *
 * The recursion guard is load-bearing, not belt-and-braces. This runs from the
 * hook that also fires every 256 bytecodes, and netif->input() below re-enters
 * the interpreter -- a socket callback scheduled by modlwip is enough. Without
 * the guard, that inner call walks the same eth_rx_cur the outer one is in the
 * middle of using: two loops advancing one shared ring pointer, handing the
 * same descriptor to lwip twice and skipping others. It corrupted memory after
 * roughly one lap of the ring, which is why a download died at the same byte
 * count every time and at any transfer rate.
 *
 * eth_lwip_lock() does not help here: it masks the Ethernet interrupt, and
 * this re-entry arrives from the same context, not from the interrupt. */
void eth_rx_process(void) {
    eth_t *self = &eth_instance;
    static bool in_rx;

    if (!self->rx_pending || in_rx) {
        return;
    }
    in_rx = true;
    self->rx_pending = false;
    eth_rx_drain(self);
    in_rx = false;
}

/******************************************************************************/
// Transmit

static err_t eth_netif_output(struct netif *netif, struct pbuf *p) {
    eth_t *self = netif->state;
    ch32h4_eth_phase = 2;

    if (!self->link_up) {
        return ERR_IF;
    }
    if (p->tot_len > ETH_MAX_PACKET_SIZE) {
        return ERR_BUF;
    }

    /* A descriptor still owned by the DMA means all four are in flight. Wait
     * briefly rather than returning an error.
     *
     * Returning ERR_WOULDBLOCK here looks tidier and behaves badly: lwip drops
     * the segment and leaves recovery to the TCP retransmit timer, so a
     * momentary four-deep backlog costs an RTO -- and a bulk send refills the
     * queue immediately, so it happens again and the connection collapses to a
     * crawl and then stalls. That is exactly what a 256 KB upload did here.
     *
     * The bound is generous against the hardware it is waiting for: four
     * maximum-size frames take about 500 us to clear at 100 Mbit, so 5 ms is
     * an order of magnitude of headroom and still finite if the DMA has
     * genuinely wedged. */
    uint32_t deadline = millis() + 5;
    while (eth_tx_cur->Status & ETH_DMATxDesc_OWN) {
        if ((int32_t)(millis() - deadline) >= 0) {
            self->stats.tx_errors++;
            return ERR_IF;
        }
    }

    /* pbuf_copy_partial flattens the chain for us; a pbuf carrying a TCP
     * segment routinely arrives as header and payload in separate links. */
    uint8_t *buf = (uint8_t *)eth_tx_cur->Buffer1Addr;
    uint16_t len = pbuf_copy_partial(p, buf, p->tot_len, 0);
    if (len != p->tot_len) {
        self->stats.tx_errors++;
        return ERR_BUF;
    }

    ch32h4_eth_last_tx = p->tot_len;
    eth_tx_cur->ControlBufferSize = len & ETH_DMATxDesc_TBS1;
    eth_tx_cur->Status |= ETH_DMATxDesc_FS | ETH_DMATxDesc_LS | ETH_DMATxDesc_OWN;

    /* Make the descriptor writes visible to the DMA before kicking it: without
     * a fence the demand poll can run while OWN is still in the core's write
     * buffer, and the DMA reads a stale descriptor. */
    __asm volatile("fence" ::: "memory");

    /* Clear TBUS before poking the demand poll. If the DMA suspended for want
     * of a descriptor, the status bit is latched, and it will not resume while
     * the bit is still set. */
    ETH->DMASR = ETH_DMASR_TBUS;
    ETH->DMATPDR = 0;
    ch32h4_eth_phase = 3;

    self->stats.tx_frames++;
    eth_tx_cur = (ETH_DMADESCTypeDef *)eth_tx_cur->Buffer2NextDescAddr;

    return ERR_OK;
}

/******************************************************************************/
// Interrupt

/* The attribute belongs on the declaration -- see ch32h4_irq.h -- and it is
 * the same "WCH-Interrupt-fast" entry every other handler on this part uses.
 * A plain __attribute__((interrupt)) here would software-save the registers
 * the hardware stack has already saved: correct, but silently different from
 * every neighbouring handler for no stated reason. */
void CH32H4_IRQ_HANDLER(ETH_IRQHandler);
void ETH_IRQHandler(void) {
    ch32h4_irq_enter(&ch32h4_irq_eth_count);
    eth_irq_handler();
    ch32h4_eth_phase = 6;
    ch32h4_irq_exit();
    ch32h4_eth_phase = 7;
}

void eth_irq_handler(void) {
    eth_t *self = &eth_instance;
    ch32h4_eth_phase = 5;
    uint32_t status = ETH->DMASR;

    /* This handler does no work beyond acknowledging the hardware and setting
     * two flags. Everything with a call chain behind it -- frame processing,
     * PHY reads, DHCP -- happens in the background hook instead. See the
     * comment on eth_rx_drain() for what putting it here cost. */
    if (status & ETH_DMASR_NIS) {
        if (status & ETH_DMASR_RS) {
            ETH->DMASR = ETH_DMASR_RS;
            self->rx_pending = true;
        }
        if (status & ETH_DMASR_TS) {
            ETH->DMASR = ETH_DMASR_TS;
        }
        if (status & ETH_DMA_FLAG_PHYSR) {
            ETH->DMASR = ETH_DMA_FLAG_PHYSR;
            self->link_event = true;
        }
        ETH->DMASR = ETH_DMASR_NIS;
    }

    if (status & ETH_DMASR_AIS) {
        if (status & ETH_DMASR_RBUS) {
            // We did not drain fast enough; eth_rx_drain() restarts the DMA.
            self->stats.rx_buf_unavail++;
            ETH->DMASR = ETH_DMASR_RBUS;
            self->rx_pending = true;
        }
        ETH->DMASR = ETH_DMASR_AIS;
    }
}

/******************************************************************************/
// lwip glue

static err_t eth_netif_init(struct netif *netif) {
    netif->linkoutput = eth_netif_output;
    netif->output = etharp_output;
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP
        | NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP;
    netif->name[0] = 'e';
    netif->name[1] = '0';
    return ERR_OK;
}

/******************************************************************************/
// Public interface

int eth_init(eth_t *self) {
    if (self->netif.input != NULL) {
        return 0;  // already initialised
    }

    /* .ethram is NOLOAD and outside .bss, so nothing has cleared it. Do it
     * here, exactly as ch32_usbd_init() does for .usbram -- a descriptor whose
     * OWN bit starts as whatever the RAM held is a very confusing bug. */
    memset(&_sethram, 0, (size_t)(&_eethram - &_sethram));

    memset(&self->stats, 0, sizeof(self->stats));
    self->active = false;
    self->link_up = false;

    eth_get_mac_addr(self->netif.hwaddr);
    self->netif.hwaddr_len = 6;

    int ret = eth_mac_init(self);
    if (ret != 0) {
        return ret;
    }

    eth_led_init();

    ip_addr_t none = { 0 };
    netif_add(&self->netif, &none, &none, &none, self,
        eth_netif_init, ethernet_input);
    /* The DHCP hostname. A sketch changes it with eth_set_hostname() before
     * eth_init(); the default is what shows up in a router's client list. */
    netif_set_hostname(&self->netif, ch32h4_eth_hostname);
    netif_set_default(&self->netif);

    NVIC_SetPriority(ETH_IRQn, 1);
    NVIC_EnableIRQ(ETH_IRQn);

    return 0;
}

int eth_start(eth_t *self) {
    int ret = eth_init(self);
    if (ret != 0) {
        return ret;
    }

    self->active = true;
    netif_set_up(&self->netif);

    /* Pick up a cable that was already plugged in before we enabled the
     * interrupt: with no link change to report, the PHY has nothing to
     * interrupt about. If negotiation has not finished yet this finds the link
     * still down, and eth_poll() picks it up a second later -- which is why
     * DHCP is started from eth_link_poll() rather than here. */
    eth_link_poll(self);

    return 0;
}

int eth_stop(eth_t *self) {
    if (!self->active) {
        return 0;
    }

    dhcp_stop(&self->netif);
    netif_set_down(&self->netif);

    ETH_MACTransmissionCmd(DISABLE);
    ETH_MACReceptionCmd(DISABLE);
    ETH_FlushTransmitFIFO();
    ETH_DMATransmissionCmd(DISABLE);
    ETH_DMAReceptionCmd(DISABLE);

    self->active = false;
    return 0;
}

bool eth_is_active(eth_t *self) {
    return self->active;
}

struct netif *eth_netif(eth_t *self) {
    return &self->netif;
}

const eth_stats_t *eth_get_stats(eth_t *self) {
    return &self->stats;
}

/* Reads clocks, MAC and PHY in one pass, so ch32.eth_diag() can say which
 * stage of bring-up failed rather than only that the link is down. Safe to
 * call before eth_init(): the register reads return zeros, which is itself the
 * answer. */
void eth_get_diag(eth_diag_t *out) {
    out->rcc_ctlr = RCC->CTLR;
    out->hbpcenr = RCC->HBPCENR;
    out->macphycr = ETH->MACPHYCR;
    out->maccr = ETH->MACCR;
    out->dmasr = ETH->DMASR;
    out->dmaomr = ETH->DMAOMR;

    out->phy_id1 = eth_phy_read(PHY_PHYIDR1);
    out->phy_id2 = eth_phy_read(PHY_PHYIDR2);
    out->phy_bcr = eth_phy_read(PHY_BCR);
    (void)eth_phy_read(PHY_BSR);  // latched-low; see eth_link_poll()
    out->phy_bsr = eth_phy_read(PHY_BSR);
    out->phy_anlpar = eth_phy_read(PHY_ANLPAR);
    out->phy_status = eth_phy_read_paged(PHY_PAGE_0, PHY_STATUS_REG);

    out->lock_depth = eth_lock_depth;
    out->irq_enabled = NVIC_GetStatusIRQ(ETH_IRQn) ? 1 : 0;
}

/* Encoding is the one network.LAN.status() uses across ports, from
 * ports/stm32/eth.c: 0 no link, 1 link but interface down, 2 link but no IP
 * yet, 3 fully up. */
int eth_link_status(eth_t *self) {
    struct netif *netif = &self->netif;
    if ((netif->flags & (NETIF_FLAG_UP | NETIF_FLAG_LINK_UP))
        == (NETIF_FLAG_UP | NETIF_FLAG_LINK_UP)) {
        return netif_ip4_addr(netif)->addr != 0 ? 3 : 2;
    }
    return self->link_up ? 1 : 0;
}

#endif /* CH32H4_ETHERNET */
