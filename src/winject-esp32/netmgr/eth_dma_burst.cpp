#include "eth_dma_burst.h"

eth_mac_dma_burst_len_t eth_dma_burst_len_from_beats(uint8_t beats)
{
    switch (beats)
    {
        case 16:
            return ETH_DMA_BURST_LEN_16;
        case 8:
            return ETH_DMA_BURST_LEN_8;
        case 4:
            return ETH_DMA_BURST_LEN_4;
        case 2:
            return ETH_DMA_BURST_LEN_2;
        case 1:
            return ETH_DMA_BURST_LEN_1;
        case 32:
        default:
            return ETH_DMA_BURST_LEN_32;
    }
}
