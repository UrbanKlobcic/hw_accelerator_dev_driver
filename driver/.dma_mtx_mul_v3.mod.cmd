savedcmd_dma_mtx_mul_v3.mod := printf '%s\n'   dma_mtx_mul_v3.o | awk '!x[$$0]++ { print("./"$$0) }' > dma_mtx_mul_v3.mod
