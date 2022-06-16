EXTRA_CFLAGS += $(CFLAGS) -I$(PWD)/../include 

ifneq ($(KERNELRELEASE),)
        obj-m := cn_test.o
else
        KDIR := /lib/modules/`uname -r`/build


all:
	make -C $(KDIR) M=$(PWD) modules
	gcc -o ucon ucon.c

clean:
	rm -rf .cn_test* .tmp* .cache* *.ko *.o *.mod.o *.mod.c *.symvers *ko.unsigned *modules.order
endif
