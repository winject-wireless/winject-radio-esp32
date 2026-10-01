#ifndef WINJECT_ETH_DMA_BURST_H_
#define WINJECT_ETH_DMA_BURST_H_

#include <stdint.h>

#include "hal/eth_types.h"

// EMAC programmed DMA burst for tune set_eth_dma_burst_len (1/2/4/8/16/32
// beats). Other values map to 32.
eth_mac_dma_burst_len_t eth_dma_burst_len_from_beats(uint8_t beats);

#endif  // WINJECT_ETH_DMA_BURST_H_
