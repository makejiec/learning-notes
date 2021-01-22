/* ir-nec-decoder.c - handle HX1838 IR Pulse/Space protocol
 *
 * Copyright (C) 2010 by Mauro Carvalho Chehab
 *
 * This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/bitrev.h>
#include <linux/module.h>
#include "rc-core-priv.h"

#define HX1838_NBITS		32
#define HX1838_UNIT		562500  /* ns */
#define HX1838_HEADER_PULSE	(16 * HX1838_UNIT)     /*起始信号低电平时长NEC协议为9mS*/
#define HX1838X_HEADER_PULSE	(8  * HX1838_UNIT) /*necx起始信号低电平时长NEC协议为4.5mS*/
#define HX1838_HEADER_SPACE	(8  * HX1838_UNIT)    /*起始信号高电平时长NEC协议为4.5mS*/
#define HX1838_REPEAT_SPACE	(4  * HX1838_UNIT)    /*重复信号高电平时长NEC协议为2.25mS*/ 
#define HX1838_BIT_PULSE		(1  * HX1838_UNIT) /*数据比特低电平时长NEC协议为0.56 mS*/
#define HX1838_BIT_0_SPACE		(1  * HX1838_UNIT)  /*数据比特0高电平时长NEC协议为0.56mS*/
#define HX1838_BIT_1_SPACE		(3  * HX1838_UNIT)  /*数据比特1高电平时长NEC协议为0.56mS*/  
#define	HX1838_TRAILER_PULSE	(1  * HX1838_UNIT)  
#define	HX1838_TRAILER_SPACE	(65 * HX1838_UNIT) /*帧结束信号，根据实际测量实际测试hx1838接收到的这个space大概35~37ms */
#define HX1838X_REPEAT_BITS	1


enum nec_state {
	STATE_INACTIVE,
	STATE_HEADER_SPACE,
	STATE_BIT_PULSE,
	STATE_BIT_SPACE,
	STATE_TRAILER_PULSE,
	STATE_TRAILER_SPACE,
};

/**
 * ir_hx1838_decode() - Decode one HX1838 pulse or space
 * @dev:	the struct rc_dev descriptor of the device
 * @duration:	the struct ir_raw_event descriptor of the pulse/space
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_hx1838_decode(struct rc_dev *dev, struct ir_raw_event ev)
{
	struct nec_dec *data = &dev->raw->nec;
	u32 scancode;
	enum rc_type rc_type;
	u8 address, not_address, command, not_command;
	bool send_32bits = false;
	static int repeat_num = 0;
	
	if (!is_timing_event(ev)) {
		if (ev.reset)
			data->state = STATE_INACTIVE;
		return 0;
	}


	switch (data->state) {
     
	case STATE_INACTIVE: //非激活状态，要等待9ms低电平+4.5ms高电平作为引导信号
		if (!ev.pulse)
			break;
		
		if (eq_margin(ev.duration, HX1838_HEADER_PULSE, HX1838_UNIT * 2)) {
			data->is_nec_x = false;
			data->necx_repeat = false;
		} else if (eq_margin(ev.duration, HX1838X_HEADER_PULSE, HX1838_UNIT / 2))
			data->is_nec_x = true;
		else
			break;
		
		data->count = 0;
		data->state = STATE_HEADER_SPACE;
		return 0;

	case STATE_HEADER_SPACE:
		if (ev.pulse)
			break;

		if (eq_margin(ev.duration, HX1838_HEADER_SPACE, HX1838_UNIT)) {
			data->state = STATE_BIT_PULSE;
			repeat_num = 0;
			return 0;
		} else if (eq_margin(ev.duration, HX1838_REPEAT_SPACE, HX1838_UNIT / 2)) {
#if 0
			//使用变量记录，连续接受到3次就认定为重复码。
		    repeat_num++;
			if (3 == repeat_num) {
				rc_repeat(dev);
				//pr_err("Repeat last key1  %dus  %s\n", TO_US(ev.duration), TO_STR(ev.pulse));
				//data->state = STATE_TRAILER_PULSE;
				repeat_num = 0;
				return 0;
			}
#endif
			/* 实际测试不需要直接保持原来代码 */
			rc_repeat(dev);
			return 0;
		}

		break;

	case STATE_BIT_PULSE:
		if (!ev.pulse)
			break;

		if (!eq_margin(ev.duration, HX1838_BIT_PULSE, HX1838_UNIT / 2))
			break;

		data->state = STATE_BIT_SPACE;
		return 0;

	case STATE_BIT_SPACE:
		if (ev.pulse) {
			//pr_err("error ev.pulse in STATE_BIT_SPACE\n");
			break;
		}
			
		if (data->necx_repeat && data->count == HX1838X_REPEAT_BITS &&
			geq_margin(ev.duration,
			HX1838_TRAILER_SPACE, HX1838_UNIT / 2)) {
				pr_err( "Repeat last key2\n");
				rc_repeat(dev);
				data->state = STATE_INACTIVE;
				return 0;

		} else if (data->count > HX1838X_REPEAT_BITS)
			data->necx_repeat = false;

		//pr_err("data_bit_count:%d data_bits:0x%08x\n", data->count, data->bits);
		data->bits <<= 1;
		if (eq_margin(ev.duration, HX1838_BIT_1_SPACE, HX1838_UNIT / 2))
			data->bits |= 1;
		else if (!eq_margin(ev.duration, HX1838_BIT_0_SPACE, HX1838_UNIT / 2))
			break;
		data->count++;

		if (data->count == HX1838_NBITS)
			data->state = STATE_TRAILER_PULSE;
		else
			data->state = STATE_BIT_PULSE;
		
		return 0;
	
	case STATE_TRAILER_PULSE:
		if (!ev.pulse)
			break;

		if (!eq_margin(ev.duration, HX1838_TRAILER_PULSE, HX1838_UNIT / 2))
			break;

		data->state = STATE_TRAILER_SPACE;
		return 0;

	case STATE_TRAILER_SPACE:
		if (ev.pulse)
			break;

		if (!geq_margin(ev.duration, 	HX1838_TRAILER_SPACE, HX1838_UNIT / 2))
			break;

		address     = bitrev8((data->bits >> 24) & 0xff);
		not_address = bitrev8((data->bits >> 16) & 0xff);
		command	    = bitrev8((data->bits >>  8) & 0xff);
		not_command = bitrev8((data->bits >>  0) & 0xff);

		if ((command ^ not_command) != 0xff) {
			pr_err( "HX1838 checksum error: received 0x%08x\n",
				   data->bits);
			send_32bits = true;
		}

		if (send_32bits) {
			/* HX1838 transport, but modified protocol, used by at
			 * least Apple and TiVo remotes */
			scancode = data->bits;
			//pr_err("HX1838 (modified) scancode 0x%08x\n", scancode);
			//pr_err("HX1838 (Ext) scancode 0x%06x\n", scancode);
			rc_type = RC_TYPE_NEC32;
		} else if ((address ^ not_address) != 0xff) {
			/* Extended HX1838 */
			scancode = address     << 16 |
				   not_address <<  8 |
				   command;
			//pr_err("HX1838 (Ext) scancode 0x%06x\n", scancode);
			rc_type = RC_TYPE_NECX;
		} else {
			/* Normal HX1838 */
			scancode = address << 8 | command;
			//pr_err("HX1838 scancode 0x%04x\n", scancode);
			//pr_err("HX1838 scancode 0x%04x\n", scancode);
			rc_type = RC_TYPE_NEC;
		}

		if (data->is_nec_x)
			data->necx_repeat = true;

		rc_keydown(dev, rc_type, scancode, 0);
		data->state = STATE_INACTIVE;
		return 0;
	}

	//pr_err("HX1838 decode failed at count %d state %d (%uus %s)\n",
	//	   data->count, data->state, TO_US(ev.duration), TO_STR(ev.pulse));
	//pr_err("HX1838 decode failed at count %d state %d (%uus %s)\n",
	//	   data->count, data->state, TO_US(ev.duration), TO_STR(ev.pulse));
	data->state = STATE_INACTIVE;
	return -EINVAL;
}

static struct ir_raw_handler nec_handler = {
	.protocols	= RC_BIT_NEC | RC_BIT_NECX | RC_BIT_NEC32,
	.decode		= ir_hx1838_decode,
};

static int __init ir_hx1838_decode_init(void)
{
	ir_raw_handler_register(&nec_handler);

	printk(KERN_INFO "IR HX1838 protocol handler initialized, allowed_protos:%llu\n", nec_handler.protocols);
	return 0;
}

static void __exit ir_hx1838_decode_exit(void)
{
	ir_raw_handler_unregister(&nec_handler);
}

module_init(ir_hx1838_decode_init);
module_exit(ir_hx1838_decode_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ping_wang@szxcz.com");
MODULE_DESCRIPTION("HX1838 IR protocol decoder");
