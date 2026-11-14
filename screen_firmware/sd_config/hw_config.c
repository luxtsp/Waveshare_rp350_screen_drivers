#include "hw_config.h"

#include "hardware/pio.h"
#include "hardware/dma.h"

/*
 * The RP2350-Touch-LCD-1.85C schematic exposes an SDIO interface:
 * SDIO_CLK, SDIO_CMD, SDIO_D0, SDIO_D1, SDIO_D2, and SDIO_D3.
 *
 * Replace these placeholders with the GPIO numbers connected to those nets.
 * The D1/D2/D3 GPIOs must be consecutive: D0 + 1, D0 + 2, D0 + 3.
 */
#define SDIO_CLK_GPIO 0
#define SDIO_CMD_GPIO 0
#define SDIO_D0_GPIO  0

static sd_sdio_if_t sdio_interface = {
    .CLK_gpio = SDIO_CLK_GPIO,
    .CMD_gpio = SDIO_CMD_GPIO,
    .D0_gpio = SDIO_D0_GPIO,

    /* These are required by the SDIO controller configuration. */
    .D1_gpio = SDIO_D0_GPIO + 1u,
    .D2_gpio = SDIO_D0_GPIO + 2u,
    .D3_gpio = SDIO_D0_GPIO + 3u,

    /*
     * The display QSPI driver also uses PIO and DMA. Start with PIO1 and
     * DMA_IRQ_1, with a shared IRQ handler, to reduce the chance of a
     * resource conflict. If the SD library reports a PIO/DMA conflict,
     * change these after checking which resources pio_qspi.c claims.
     */
    .SDIO_PIO = pio1,
    .DMA_IRQ_num = DMA_IRQ_1,
    .use_exclusive_DMA_IRQ_handler = false,

    .baud_rate = 25 * 1000 * 1000,

    .set_drive_strength = false
};

static sd_card_t sd_card = {
    .type = SD_IF_SDIO,
    .sdio_if_p = &sdio_interface,

    /* Set to true only if the board has a separate card-detect GPIO. */
    .use_card_detect = false
};

size_t sd_get_num(void) {
    return 1u;
}

sd_card_t *sd_get_by_num(size_t num) {
    if (num != 0u) {
        return NULL;
    }

    return &sd_card;
}
