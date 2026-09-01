/* The SDMMC block device: 512-byte blocks, nothing above them.
 *
 * The filesystem sits on top of this in the SD library. Keeping the block
 * layer separate is what lets the card be tested -- identified, read, written,
 * verified -- without a filesystem in the way, which matters because almost
 * every way this can fail fails down here.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CH32H4_SD_BLOCK_SIZE  512

/* Negative returns, so a caller can tell them apart without errno. */
#define CH32H4_SD_OK          0
#define CH32H4_SD_ETIMEDOUT   (-1)
#define CH32H4_SD_EIO         (-2)
#define CH32H4_SD_ENODEV      (-3)
#define CH32H4_SD_ERANGE      (-4)
#define CH32H4_SD_EVOLTAGE    (-5)   /* VIO18 too low for the card to answer */
#define CH32H4_SD_EPARAM      (-6)

typedef enum {
    CH32H4_SD_CARD_NONE = 0,
    CH32H4_SD_CARD_SDSC,   /* standard capacity, byte addressed */
    CH32H4_SD_CARD_SDHC,   /* high/extended capacity, block addressed */
} ch32h4_sd_card_type_t;

typedef struct {
    uint32_t block_count;
    uint32_t freq;          /* what was actually programmed, not what was asked */
    uint16_t rca;
    uint8_t width;
    uint8_t card_type;
    bool initialised;
    bool high_speed;
    uint32_t cid[4];
    uint32_t csd[4];
} ch32h4_sd_t;

/* The last identification's command trace: the command index, the interrupt
 * flag word it finished with, and its short response. Cleared at the start of
 * every identification, so a dump after a failed begin() describes that
 * attempt. A card that will not identify is close to intractable without
 * this. */
#define CH32H4_SD_LOG_MAX 24
typedef struct {
    uint8_t cmd;
    uint16_t flags;
    uint32_t resp;
} ch32h4_sd_log_entry_t;
extern ch32h4_sd_log_entry_t ch32h4_sd_log[CH32H4_SD_LOG_MAX];
extern uint8_t ch32h4_sd_log_n;

/* One controller, so one card. */
extern ch32h4_sd_t ch32h4_sd;

/* width is 1 or 4; freq is a ceiling, and the driver reports what it reached.
 * Safe to call again -- it deinitialises first. */
/* Raw controller registers, for diagnosing a card that will not identify:
 * 0 CONTROL, 1 CLK_DIV, 2 STATUS, 3 INT_FG, 4 RCC->HBRSTR, 5 RCC->HBPCENR. */
uint32_t ch32h4_sd_debug(uint8_t which);

int ch32h4_sd_begin(uint8_t width, uint32_t freq);
void ch32h4_sd_end(void);

int ch32h4_sd_read_blocks(uint32_t block, uint8_t *buf, uint32_t nblocks);
int ch32h4_sd_write_blocks(uint32_t block, const uint8_t *buf, uint32_t nblocks);

static inline bool ch32h4_sd_ready(void) { return ch32h4_sd.initialised; }

#ifdef __cplusplus
}
#endif
