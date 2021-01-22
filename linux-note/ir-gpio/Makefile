
KERN_DIR=/*/linux-4.9.y
CUR_DIR=$(shell pwd)
OUT_PUT_DIR=$(CUR_DIR)/out

obj-m += rc-main.o rc-ir-raw.o
obj-m += gpio-ir-recv.o
obj-m += ir-hx1838-decoder.o


test_app_name = evtest
tets_app_src = evtest.c
CC = arm-himix100-linux-gcc
INC_PATH +=
CFLAGS +=
LDFLAGS += -pthread


all:
	@ if [ ! -d ${OUT_PUT_DIR}  ]; then  mkdir -p ${OUT_PUT_DIR}; fi
	make -C $(KERN_DIR) M=$(CUR_DIR) modules V=1
	$(CC) $(INC_PATH) $(CFLAGS) $(LDFLAGS) $(tets_app_src)  -o $(test_app_name) 
	cp *.ko   $(test_app_name) ${OUT_PUT_DIR}

.PHONY:clean
clean:
	make  -C $(KERN_DIR) M=$(CUR_DIR)  modules clean
	rm ${OUT_PUT_DIR}/* $(test_app_name) -rf

