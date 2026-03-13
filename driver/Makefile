#obj-m += mtx_mul.o
#obj-m += dma_mtx_mul.o
#obj-m += dma_mtx_mul_v2.o
obj-m += dma_mtx_mul_v3.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean