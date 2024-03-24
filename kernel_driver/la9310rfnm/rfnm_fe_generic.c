
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/rfnm-shared.h>
#include <linux/rfnm-gpio.h>


void rfnm_fe_load_latches(struct rfnm_dgb * dgb_dt);
void rfnm_fe_trigger_latches(struct rfnm_dgb * dgb_dt);
void rfnm_fe_manual_clock(int dgb_id, int id);


int rfnm_fe_generic_init(struct rfnm_dgb * dgb_dt, int * num_latches) {

	int i;

    rfnm_gpio_output(dgb_dt->dgb_id, RFNM_DGB_LA_FE_CLK);
	rfnm_gpio_output(dgb_dt->dgb_id, RFNM_DGB_GPIO2_2); 
	rfnm_gpio_output(dgb_dt->dgb_id, RFNM_DGB_GPIO2_3);


#if 1
	rfnm_gpio_output(dgb_dt->dgb_id, RFNM_DGB_GPIO4_22);
	rfnm_gpio_output(dgb_dt->dgb_id, RFNM_DGB_GPIO4_23);
	rfnm_gpio_output(dgb_dt->dgb_id, RFNM_DGB_GPIO4_31);

	rfnm_gpio_output(dgb_dt->dgb_id, RFNM_DGB_GPIO4_9);

	rfnm_gpio_output(dgb_dt->dgb_id, RFNM_DGB_GPIO4_0);
	rfnm_gpio_output(dgb_dt->dgb_id, RFNM_DGB_GPIO4_1);
	rfnm_gpio_output(dgb_dt->dgb_id, RFNM_DGB_GPIO4_2);
	rfnm_gpio_output(dgb_dt->dgb_id, RFNM_DGB_GPIO4_4);
	rfnm_gpio_output(dgb_dt->dgb_id, RFNM_DGB_GPIO4_5);

#endif 
#if 1

	rfnm_gpio_clear(dgb_dt->dgb_id, RFNM_DGB_GPIO4_22);
	rfnm_gpio_clear(dgb_dt->dgb_id, RFNM_DGB_GPIO4_23);
	rfnm_gpio_clear(dgb_dt->dgb_id, RFNM_DGB_GPIO4_31);

	rfnm_gpio_clear(dgb_dt->dgb_id, RFNM_DGB_GPIO4_9);

	rfnm_gpio_clear(dgb_dt->dgb_id, RFNM_DGB_GPIO4_0);
	rfnm_gpio_clear(dgb_dt->dgb_id, RFNM_DGB_GPIO4_1);
	rfnm_gpio_clear(dgb_dt->dgb_id, RFNM_DGB_GPIO4_2);
	rfnm_gpio_clear(dgb_dt->dgb_id, RFNM_DGB_GPIO4_4);
	rfnm_gpio_clear(dgb_dt->dgb_id, RFNM_DGB_GPIO4_5);

#endif

	rfnm_gpio_set(dgb_dt->dgb_id, RFNM_DGB_GPIO2_2); // ff mr
	rfnm_gpio_clear(dgb_dt->dgb_id, RFNM_DGB_GPIO2_3); //ff oe

	memset(&dgb_dt->fe.latch_val, 0, sizeof(dgb_dt->fe.latch_val));
	memset(&dgb_dt->fe.latch_val_last_written, 0xff, sizeof(dgb_dt->fe.latch_val_last_written));
	memcpy(&dgb_dt->fe.num_latches, num_latches, sizeof(dgb_dt->fe.num_latches));






	//rfnm_fe_load_latches(dgb_dt);
	//rfnm_fe_trigger_latches(dgb_dt);


	// trigger all latches to set initial status
	for(i = 1; i < 7; i++) {
		rfnm_fe_manual_clock(dgb_dt->dgb_id, i);
	}





	return 0;
}

// phy demux 1 gpio4 22
// phy demux 2 gpio4 23
// phy demux 3 gpio4 31

// gate rba gpio4 28

void rfnm_fe_manual_clock(int dgb_id, int id) {

	printk("clocking %d\n", id);

	// enable latch before changing output    
	rfnm_gpio_clear(dgb_id, RFNM_DGB_LA_FE_CLK);

	// change output
	switch(id) {
	case 1:
		rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_22);
		rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_23);
		rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_31);
		break;
	case 2:
		rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_22);
		rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_23);
		rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_31);
		break;
	case 3:
		rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_22);
		rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_23);
		rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_31);
		break;
	case 4:
		rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_22);
		rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_23);
		rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_31);
		break;
	case 5:
		rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_22);
		rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_23);
		rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_31);
		break;
	case 6:
		rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_22);
		rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_23);
		rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_31);
		break;
	case 7:
		rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_22);
		rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_23);
		rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_31);
		break;
	}

	// latch read-in
    rfnm_gpio_set(dgb_id, RFNM_DGB_LA_FE_CLK);
	rfnm_gpio_clear(dgb_id, RFNM_DGB_LA_FE_CLK);
}

void rfnm_fe_load_data_bit(int dgb_id, int latch, int bit) {
	if(bit) {
		rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_9);
	} else {
		rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_9);
	}

	int latch_datain_clock_gpio;

	switch(latch) {
	case 1:
		//latch_datain_clock_gpio = 0;
		rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_0);
		rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_0);
		break;
	case 2:
		//latch_datain_clock_gpio = 1;
		rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_1);
		rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_1);
		break;
	case 3:
		//latch_datain_clock_gpio = 2;
		rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_2);
		rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_2);
		break;
	case 4:
		//latch_datain_clock_gpio = 4;
		rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_4);
		rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_4);
		break;
	case 5:
		//latch_datain_clock_gpio = 5;
		rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_5);
		rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_5);
		break;
	}

	//*gpio4_dr |= (1 << latch_datain_clock_gpio);
	//*gpio4_dr &= ~(1 << latch_datain_clock_gpio);

	

}

void rfnm_fe_srb(struct rfnm_dgb * dgb_dt, int bit, int val) {
	int latch_seq = ((bit >> 24) & 0xf) - 1;
	int latch_q = (bit >> 16) & 0x1f;
	int latch_num = ((bit >> 28) & 0x1f) - 1;

	if(val) {
		dgb_dt->fe.latch_val[latch_num] |= 1 << (latch_q + (latch_seq * 8));
	} else {
		dgb_dt->fe.latch_val[latch_num] &= ~(1 << (latch_q + (latch_seq * 8)));
	}

	//printk("seq %d q %d latch %d val %d stored %x\n", latch_seq, latch_q, latch_num, val, dgb_dt->fe.latch_val[latch_num]);
}


void rfnm_fe_load_latch(struct rfnm_dgb * dgb_dt, int id) {

	uint32_t lval = dgb_dt->fe.latch_val[id - 1];

	if(lval == dgb_dt->fe.latch_val_last_written[id - 1]) {
		return;
	}

	printk("loading latch %d, %x\n", id, lval);

	//printk("bits ");
	int l, q, bit;
	for(l = dgb_dt->fe.num_latches[id] - 1; l >= 0; l--) {
		for(q = 7; q >= 0; q--) {
			bit = (lval & (1 << ((8 * l) + q)) ) >> ((8 * l) + q);
			rfnm_fe_load_data_bit(dgb_dt->dgb_id, id, bit);
			//printk("%d ", bit);
		}
	}

	//printk("\n");

	//rfnm_fe_manual_clock(id);
}


void rfnm_fe_load_latches(struct rfnm_dgb * dgb_dt) {
	int i;
	for(i = 1; i < 7; i++) {		
		rfnm_fe_load_latch(dgb_dt, i); 
	}
}

void rfnm_fe_trigger_latches(struct rfnm_dgb * dgb_dt) {
	int i = 0, clear_with_last_latch = 0;
	if(dgb_dt->fe.load_order[0]) {
		while(dgb_dt->fe.load_order[i]) {
			int lid = dgb_dt->fe.load_order[i] - 1;
			if(dgb_dt->fe.latch_val_last_written[lid] != dgb_dt->fe.latch_val[lid]) {
				rfnm_fe_manual_clock(dgb_dt->dgb_id, lid + 1);
				dgb_dt->fe.latch_val_last_written[lid] = dgb_dt->fe.latch_val[lid];
				clear_with_last_latch = 1;
			}
			i++;
		}
	} else {
		for(i = 1; i < 7; i++) {
			if(dgb_dt->fe.latch_val_last_written[i - 1] != dgb_dt->fe.latch_val[i - 1]) {
				rfnm_fe_manual_clock(dgb_dt->dgb_id, i);
				dgb_dt->fe.latch_val_last_written[i - 1] = dgb_dt->fe.latch_val[i - 1];
				clear_with_last_latch = 1;
			}
		}
	}
	
	if(clear_with_last_latch) {
		rfnm_fe_manual_clock(dgb_dt->dgb_id, 7);
	}
}


void rfnm_fe_load_order(struct rfnm_dgb * dgb_dt, int l0, int l1, int l2, int l3, int l4, int l5, int l6, int l7) {
	dgb_dt->fe.load_order[0] = l0;
	dgb_dt->fe.load_order[1] = l1;
	dgb_dt->fe.load_order[2] = l2;
	dgb_dt->fe.load_order[3] = l3;
	dgb_dt->fe.load_order[4] = l4;
	dgb_dt->fe.load_order[5] = l5;
	dgb_dt->fe.load_order[6] = l6;
	dgb_dt->fe.load_order[7] = l7;
}

