obj-m += vtfs.o

vtfs-objs := source/vtfs.o source/impl/vtfs_ram_impl.o source/impl/vtfs_net_impl.o

PWD := $(CURDIR) 
KDIR = /lib/modules/`uname -r`/build
EXTRA_CFLAGS = -Wall -g

all:
	make -C $(KDIR) M=$(PWD) modules 

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -rf .cache