PWD           := $(shell pwd)
DRV_NAME      := nct5104_wdt

obj-m += $(DRV_NAME).o
$(DRV_NAME)-objs := $(DRV_OBJS)

# Let KERNEL_SRC take priority over KDIR
# $(KDIR) is used in official kernel documentation:
# https://www.kernel.org/doc/Documentation/kbuild/modules.txt
# and $(KERNEL_SRC) is used in Yocto, documented at:
# https://www.yoctoproject.org/docs/2.4.1/mega-manual/mega-manual.html#working-with-out-of-tree-modules
ifdef KERNEL_SRC
KDIR=$(KERNEL_SRC)
endif

.PHONY: clean modules_install

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules 

clean: 
	$(MAKE) -C $(KDIR) M=$(PWD) clean

modules_install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
