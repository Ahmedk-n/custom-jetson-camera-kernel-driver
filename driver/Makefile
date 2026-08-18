# Out-of-tree build of the e-CAM200 driver against L4T R39 / kernel 6.8.
# Needs the nvidia-public headers + Module.symvers shipped with JetPack 7.
obj-m += e-con_cam.o
e-con_cam-objs := cam_common.o

KDIR ?= /lib/modules/$(shell uname -r)/build
NV   ?= /usr/src/nvidia/nvidia-public
ccflags-y += -I$(src) -I$(NV)/include -I$(NV)/drivers/media/platform/tegra

all:
	$(MAKE) -C $(KDIR) M=$(PWD) KBUILD_EXTRA_SYMBOLS=$(NV)/Module.symvers modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
