/* Ethernet MAC and on-chip 100M PHY.
 *
 * This header is the whole interface between the driver and the rest of the
 * port; network_lan.c reaches the hardware only through these calls.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Not for the declarations -- for the dependency resolver. See the header
   itself: it is the only name arduino-cli can match to find lwIP. */
#include "lwip_arduino.h"
#include "lwip/netif.h"

typedef struct _eth_t eth_t;

extern eth_t eth_instance;

/* Bring the MAC, the PHY and the lwip netif up far enough that the interface
 * exists and can be inspected. Does not start the DMA -- eth_start() does.
 * Returns 0, or a negative errno. */
int eth_init(eth_t *self);

/* The name this interface gives DHCP, and so what appears in a router's
 * client list. Set it before eth_init(); lwIP stores the pointer, so the
 * string has to outlive the interface. */
extern const char *ch32h4_eth_hostname;

int eth_start(eth_t *self);
int eth_stop(eth_t *self);
bool eth_is_active(eth_t *self);

/* Link state as the PHY reports it, independent of whether an IP is
 * configured. */
bool eth_link_is_up(eth_t *self);

/* Combined link + IP state, in the encoding network.LAN.status() wants. */
int eth_link_status(eth_t *self);

struct netif *eth_netif(eth_t *self);

/* Raw PHY register access, exposed for diagnostics. */
uint16_t eth_phy_read(uint16_t reg);
void eth_phy_write(uint16_t reg, uint16_t val);

/* Counters behind ch32.eth_diag(). Frames in, frames out, and the three ways
 * the hardware tells us it lost something. */
typedef struct _eth_stats_t {
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t rx_dropped;      // no pbuf, or frame larger than a buffer
    uint32_t rx_buf_unavail;  // DMA ran out of descriptors (RBU)
    uint32_t tx_errors;
    uint32_t link_changes;
} eth_stats_t;

const eth_stats_t *eth_get_stats(eth_t *self);

/* Raw hardware state behind ch32.eth_diag(). Every field is read live; this is
 * the view that turns "the link is down" into a specific answer about which
 * stage of bring-up did not happen. */
typedef struct _eth_diag_t {
    uint32_t rcc_ctlr;    // bit 26 ETH_PLLON, bit 27 ETH_PLLRDY
    uint32_t hbpcenr;     // bit 14 ETH MAC clock
    uint32_t macphycr;    // bit 30 PHY power-down, bit 31 PHY reset-n
    uint32_t maccr;
    uint32_t dmasr;
    uint32_t dmaomr;
    uint16_t phy_id1;     // 0 or 0xffff here means SMI is not talking
    uint16_t phy_id2;
    uint16_t phy_bcr;
    uint16_t phy_bsr;
    uint16_t phy_anlpar;
    uint16_t phy_status;  // vendor register: live speed and duplex
    uint32_t lock_depth;  // nonzero at rest means a leaked MICROPY_PY_LWIP_ENTER
    uint32_t irq_enabled; // 0 here at rest means the receive path is dead
} eth_diag_t;

void eth_get_diag(eth_diag_t *out);

void eth_irq_handler(void);

/* Background work: link changes flagged by the interrupt, plus a periodic
 * re-check. Called from the port's event hook, under eth_lwip_lock(). */
void eth_poll(void);

/* Hands received frames to lwip. Deliberately not done in the interrupt --
 * see the comment on eth_rx_drain(). Also called under eth_lwip_lock(). */
void eth_rx_process(void);

/* Masks the Ethernet interrupt so lwip is not re-entered from the receive
 * path. Only for regions that cannot raise -- see mpconfigport.h. */
void eth_lwip_lock(void);
void eth_lwip_unlock(void);

#ifdef __cplusplus
}
#endif
