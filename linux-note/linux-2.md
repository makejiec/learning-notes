## Hi3556v200配置RTL8189FS

参考文档：海思官方提供的《WIFI使用指南》，其他文档和网站<br/>
之前先检查下WiFi的引脚是否连接好了，这里由于我之前模组没焊好，导致之后管脚复用一直不成功。

### 第一步：将厂商提供的WiFi驱动文件夹放在SDK的下任意比较方便的位置

这里放置在<font size=5 color=red>/osdrv/opensource/</font>下
![批注 2020-07-08 134305](https://tva4.sinaimg.cn/mw690/006p97Wqgy1ggjihxffv8j30j305v0sx.jpg)

### 第二步：配置内核

进入内核目录：make ARCH=arm CROSS_COMPILE=arm-himix100-linux- menuconfig

#### 配置 CFG80211 :
![批注 2020-07-08 135454](https://tvax2.sinaimg.cn/mw690/006p97Wqgy1ggjittqj0vj30nj09nwf9.jpg)

#### 配置 GPIO :
![批注 2020-07-08 135827](https://tva2.sinaimg.cn/mw690/006p97Wqgy1ggjixk8hc6j30j40a7755.jpg)

最后还要修改下：.config里面的 <font size=5 color =red>CONFIG_RFKILL</font>，要不然在后面出现如下图的提示：rfkill: Cannot open RFKILL control device 
![批注 2020-07-08 153928](https://tva2.sinaimg.cn/mw690/006p97Wqgy1ggjsvecvcej30kc024wed.jpg)
```c
710:CONFIG_RFKILL=y
711:CONFIG_RFKILL_INPUT=y
712:CONFIG_RFKILL_GPIO=y
```

#### SDIO中断配置：<br/>
 <font size=5 color =red>/osdrv/opensource/kernel/linux-4.9.y/arch/arm/boot/dts</font>下的hi3556v200.dtsi文件中，找到mmc2，加入如图所示的cap-sdio-irq；
![批注 2020-07-08 182126](https://tvax3.sinaimg.cn/mw690/006p97Wqgy1ggjso6ygpuj30ph0f1q43.jpg)


#### 配置 Boot

有两种方法：<br/>
第一种：这里查看原理图和通过 Hi3556V200_PINOUT_CN.xlsx 的描述,配置在 <font size=5 color =red>osdrv/tools/pc/uboot_tools</font> 目录下的 Hi3556V200-DMEB-DDR3_1800M_16bit_128MB-A7_900M-SYSBUS_300M.xlsm 文件，将SDIO1的各个IO复用为sdio1的功能。
![批注 2020-07-08 193356](https://tvax4.sinaimg.cn/mw690/006p97Wqgy1ggjsmrb1ebj30qy06274n.jpg)

第二种方法：不用修改表格在，直接修改代码<font size=5 color =red>/reference/samplecam/modules/init/amp/liteos/src/hi_product_init_hi3559v200.c</font> 中，大约在380行添加如下的代码
```c
static void sdio1_wifi_pin_mux(){
	himm(0x112f0008, 0x681);
	himm(0x112f000c, 0x581);
	himm(0x112f0010, 0x581);
	himm(0x112f0014, 0x581);
	himm(0x112f0018, 0x581);
	himm(0x114F001C, 0x581);
}
```
并在下面的函数中调用
![批注 2020-07-08 192655](https://tva2.sinaimg.cn/mw690/006p97Wqgy1ggjsseawnkj30kv0ffab5.jpg)

配置修改完成后重新编一下kernel：make ARCH=arm CROSS_COMPILE=arm-himix100-linux- uImage

### 第三步：编译WiFi驱动
进入驱动放置的同级目录下执行：
```c
1.make ARCH=arm CROSS_COMPILE=arm-himix100-linux- clean
2.make -C rtl8189FS/ ARCH=arm CROSS_COMPILE=arm-himix100-linux- KSRC=/home/*****/usbcam_hi3556v200_imx335/osdrv/opensource/kernel/linux-4.9.y/
```
PS：这个KSRC为kernel的绝对路径

编译完成后可以看到rtl8189FS目录下生成了rtl8192eu.ko文件。

### 第四步：编译WiFi工具

根据官方文档进行下载并编译等操作

PS：找一些老版本安装，要不然安装出来的wpa_supplicant太大，最后flash放不下

编译完成后将其中有用的文件放到一个文件下
![批注 2020-07-08 195504](https://tva2.sinaimg.cn/mw690/006p97Wqgy1ggjt8jf3bfj30mz05xglr.jpg)

![批注 2020-07-08 195549](https://tvax2.sinaimg.cn/mw690/006p97Wqgy1ggjt9dni06j30n50753ys.jpg)

### 第五步：WiFi测试

首先将需要的工具文件打包到板子目录下，就直接在SDK下 <font size=5 color =red>\reference\samplecam\rootfs</font> 目录下的Makefile 文件中修改
![批注 2020-07-08 195930](https://tvax3.sinaimg.cn/mw690/006p97Wqgy1ggjtd56t33j31d80j20vf.jpg)

注意这几文件都需要

PS: 另外连接AP时还需要udhcpc 这个文件，先到根文件系统下的sbin目录下看看有没有，如果没有就去busybox里面配置编译下，将生成的udhcpc放到单板目录下（如上图）。

### 第六步：连接AP

进入开发板后，先到WiFi驱动模块目录下安装模块<br/>
`insmod rtl8192eu.ko`<br/>
接着创建软链接<br/>
`ln -s libnl.so.1.1 libnl.so.1`<br/>
打开WiFi：<br/>
`ifconfig wlan0 up`<br/>
使用wpa_passphrase配置wifi并加密密码<br/>
`./wpa_passphrase wifi名 密码 >> /etc/wpa_supplicant.conf`<br/>
启动 wpa_supplicant 进程<br/>
`./wpa_supplicant -i wlan0 -D nl80211 -c /etc/wpa_supplicant.conf -B`<br/>
自动获取IP地址<br/>
`udhcpc -i wlan0`

PS:这里可以提前在外面设置好配置文件wpa_supplicant.conf，参考网上的，在里面配置如下：
```c
ctrl_interface=/var/run/wpa_supplicant

network={
        ssid="iPhone"
        #psk="123456789"
        psk=4533d2b21868ec5e40c7a89e5fabff4ccbe17804bfce566cf1c26074ebf77591
}
```
这样就剩了wpa_passphrase这一步了，或者还可以通过wpa_cli来扫描热点进行配置，具体步骤在官方文档中很详细。



最后ifconfig看下分配的ip和网关之类的
![批注 2020-07-08 201814](https://tva1.sinaimg.cn/mw690/006p97Wqgy1ggjtwlyzvhj30jl04vglt.jpg)
显示分配成功和ping以下网关和外网看是否成功

![批注 2020-07-08 201926](https://tvax2.sinaimg.cn/mw690/006p97Wqgy1ggjtxva1nuj30gl061glw.jpg)






