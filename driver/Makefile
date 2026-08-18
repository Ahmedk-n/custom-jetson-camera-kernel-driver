ccflags-y += -I$(srctree)/drivers/media/platform/tegra/ -I$(srctree)/../nvidia/include/
e-con_cam-objs := cam_common.o

obj-m += e-con_cam.o
e-con_cam:
	make -C $(KERNEL_PATH) M=$(PWD) modules
clean:
	make -C $(KERNEL_PATH) M=$(PWD) clean
