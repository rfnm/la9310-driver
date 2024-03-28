// SPDX-License-Identifier: GPL-2.0+
//
//  Copyright (C) 2000-2001 Deep Blue Solutions
//  Copyright (C) 2002 Shane Nay (shane@minirl.com)
//  Copyright (C) 2006-2007 Pavel Pisa (ppisa@pikron.com)
//  Copyright (C) 2008 Juergen Beisert (kernel@pengutronix.de)

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clockchips.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/sched_clock.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <soc/imx/timer.h>
#include <linux/init.h>

#include <linux/module.h>
#include <linux/platform_device.h>

#include <linux/rfnm-shared.h>
#include <linux/rfnm-gpio.h>

/*
 * There are 4 versions of the timer hardware on Freescale MXC hardware.
 *  - MX1/MXL
 *  - MX21, MX27.
 *  - MX25, MX31, MX35, MX37, MX51, MX6Q(rev1.0)
 *  - MX6DL, MX6SX, MX6Q(rev1.1+)
 */

/* defines common for all i.MX */
#define MXC_TCTL		0x00
#define MXC_TCTL_TEN		(1 << 0) /* Enable module */
#define MXC_TPRER		0x04

/* MX31, MX35, MX25, MX5, MX6 */
#define V2_TCTL_WAITEN		(1 << 3) /* Wait enable mode */
#define V2_TCTL_CLK_IPG		(1 << 6)
#define V2_TCTL_CLK_PER		(2 << 6)
#define V2_TCTL_CLK_OSC_DIV8	(5 << 6)
#define V2_TCTL_FRR		(1 << 9)
#define V2_TCTL_24MEN		(1 << 10)
#define V2_TPRER_PRE24M		12
#define V2_IR			0x0c
#define V2_TSTAT		0x08
#define V2_TSTAT_OF1		(1 << 0)
#define V2_TCN			0x24
#define V2_TCMP			0x10

#define V2_TIMER_RATE_OSC_DIV8	3000000

struct imx_timer {
	enum imx_gpt_type type;
	void __iomem *base;
	int irq;
	struct clk *clk_per;
	struct clk *clk_ipg;
	const struct imx_gpt_data *gpt;
	struct clock_event_device ced;
};

struct imx_gpt_data {
	int reg_tstat;
	int reg_tcn;
	int reg_tcmp;
	void (*gpt_setup_tctl)(struct imx_timer *imxtm);
	void (*gpt_irq_enable)(struct imx_timer *imxtm);
	void (*gpt_irq_disable)(struct imx_timer *imxtm);
	void (*gpt_irq_acknowledge)(struct imx_timer *imxtm);
	int (*set_next_event)(unsigned long evt,
			      struct clock_event_device *ced);
};

static inline struct imx_timer *to_imx_timer(struct clock_event_device *ced)
{
	return container_of(ced, struct imx_timer, ced);
}

static void imx31_gpt_irq_disable(struct imx_timer *imxtm)
{
	writel_relaxed(0, imxtm->base + V2_IR);
}
#define imx6dl_gpt_irq_disable imx31_gpt_irq_disable

static void imx31_gpt_irq_enable(struct imx_timer *imxtm)
{
	writel_relaxed(1<<0, imxtm->base + V2_IR);
}
#define imx6dl_gpt_irq_enable imx31_gpt_irq_enable


static void imx31_gpt_irq_acknowledge(struct imx_timer *imxtm)
{
	writel_relaxed(V2_TSTAT_OF1, imxtm->base + V2_TSTAT);
}
#define imx6dl_gpt_irq_acknowledge imx31_gpt_irq_acknowledge

static void __iomem *sched_clock_reg;

static u64 notrace mxc_read_sched_clock(void)
{
	return sched_clock_reg ? readl_relaxed(sched_clock_reg) : 0;
}

//#if defined(CONFIG_ARM)
//static struct delay_timer imx_delay_timer;

static unsigned long imx_read_current_timer(void)
{
	return readl_relaxed(sched_clock_reg);
}
//#endif

static int __init mxc_clocksource_init(struct imx_timer *imxtm)
{
	unsigned int c = clk_get_rate(imxtm->clk_per);
	void __iomem *reg = imxtm->base + imxtm->gpt->reg_tcn;

#if defined(CONFIG_ARM)
//	imx_delay_timer.read_current_timer = &imx_read_current_timer;
//	imx_delay_timer.freq = c;
//	register_current_timer_delay(&imx_delay_timer);
#endif

	sched_clock_reg = reg;


    return 0;
	//sched_clock_register(mxc_read_sched_clock, 32, c);
	
    
    //return clocksource_mmio_init(reg, "mxc_timer1", c, 200, 32,
	//		clocksource_mmio_readl_up);
}

/* clock event */

static int v2_set_next_event(unsigned long evt,
			      struct clock_event_device *ced)
{
	struct imx_timer *imxtm = to_imx_timer(ced);
	unsigned long tcmp;

	tcmp = readl_relaxed(imxtm->base + V2_TCN) + evt;

	writel_relaxed(tcmp, imxtm->base + V2_TCMP);

	return evt < 0x7fffffff &&
		(int)(tcmp - readl_relaxed(imxtm->base + V2_TCN)) < 0 ?
				-ETIME : 0;
}

static int mxc_shutdown(struct clock_event_device *ced)
{
	struct imx_timer *imxtm = to_imx_timer(ced);
	u32 tcn;

	/* Disable interrupt in GPT module */
	imxtm->gpt->gpt_irq_disable(imxtm);

	tcn = readl_relaxed(imxtm->base + imxtm->gpt->reg_tcn);
	/* Set event time into far-far future */
	writel_relaxed(tcn - 3, imxtm->base + imxtm->gpt->reg_tcmp);

	/* Clear pending interrupt */
	imxtm->gpt->gpt_irq_acknowledge(imxtm);

#ifdef DEBUG
	printk(KERN_INFO "%s: changing mode\n", __func__);
#endif /* DEBUG */

	return 0;
}

static int mxc_set_oneshot(struct clock_event_device *ced)
{
	struct imx_timer *imxtm = to_imx_timer(ced);

	/* Disable interrupt in GPT module */
	imxtm->gpt->gpt_irq_disable(imxtm);

	if (!clockevent_state_oneshot(ced)) {
		u32 tcn = readl_relaxed(imxtm->base + imxtm->gpt->reg_tcn);
		/* Set event time into far-far future */
		writel_relaxed(tcn - 3, imxtm->base + imxtm->gpt->reg_tcmp);

		/* Clear pending interrupt */
		imxtm->gpt->gpt_irq_acknowledge(imxtm);
	}

#ifdef DEBUG
	printk(KERN_INFO "%s: changing mode\n", __func__);
#endif /* DEBUG */

	/*
	 * Do not put overhead of interrupt enable/disable into
	 * mxc_set_next_event(), the core has about 4 minutes
	 * to call mxc_set_next_event() or shutdown clock after
	 * mode switching
	 */
	imxtm->gpt->gpt_irq_enable(imxtm);

	return 0;
}

/*
 * IRQ handler for the timer
 */
static irqreturn_t mxc_timer_interrupt(int irq, void *dev_id)
{
	//struct clock_event_device *ced = dev_id;
	//struct imx_timer *imxtm = to_imx_timer(ced);
	uint32_t tstat;

    struct imx_timer *imxtm = dev_id;

	imxtm->gpt->gpt_irq_acknowledge(imxtm);

    rfnm_gpio_set(1, RFNM_DGB_GPIO5_16);
    rfnm_gpio_clear(1, RFNM_DGB_GPIO5_16);

	//tstat = readl_relaxed(imxtm->base + imxtm->gpt->reg_tstat);
    //u32 tcn = imx_read_current_timer();
    //u32 tcmp = readl_relaxed(imxtm->base + V2_TCMP);

	return IRQ_HANDLED;
}

static int __init mxc_clockevent_init(struct imx_timer *imxtm)
{
	return request_irq(imxtm->irq, mxc_timer_interrupt,
			   IRQF_TIMER | IRQF_IRQPOLL /*| IRQF_TRIGGER_RISING | IRQF_EARLY_RESUME*/, "RFNM TTI Tick", imxtm);
}

static void imx6dl_gpt_setup_tctl(struct imx_timer *imxtm)
{
	u32 tctl_val;

	tctl_val = V2_TCTL_FRR | V2_TCTL_WAITEN | MXC_TCTL_TEN;
	if (clk_get_rate(imxtm->clk_per) == V2_TIMER_RATE_OSC_DIV8) {
		tctl_val |= V2_TCTL_CLK_OSC_DIV8;
		/* 24 / 8 = 3 MHz */
		writel_relaxed(7 << V2_TPRER_PRE24M, imxtm->base + MXC_TPRER);
		tctl_val |= V2_TCTL_24MEN;
	} else {
		tctl_val |= V2_TCTL_CLK_PER;
	}

	writel_relaxed(tctl_val, imxtm->base + MXC_TCTL);
}

static const struct imx_gpt_data imx6dl_gpt_data = {
	.reg_tstat = V2_TSTAT,
	.reg_tcn = V2_TCN,
	.reg_tcmp = V2_TCMP,
	.gpt_irq_enable = imx6dl_gpt_irq_enable,
	.gpt_irq_disable = imx6dl_gpt_irq_disable,
	.gpt_irq_acknowledge = imx6dl_gpt_irq_acknowledge,
	.gpt_setup_tctl = imx6dl_gpt_setup_tctl,
	.set_next_event = v2_set_next_event,
};

static int __init _mxc_timer_init(struct imx_timer *imxtm)
{
	int ret;

	imxtm->gpt = &imx6dl_gpt_data;

	if (IS_ERR(imxtm->clk_per)) {
		pr_err("RFNM TTI Tick: unable to get clk\n");
		return PTR_ERR(imxtm->clk_per);
	}

//	if (!IS_ERR(imxtm->clk_ipg))
//		clk_prepare_enable(imxtm->clk_ipg);
//	clk_prepare_enable(imxtm->clk_per);

//	writel_relaxed(0, imxtm->base + MXC_TCTL);
//	writel_relaxed(0, imxtm->base + MXC_TPRER); /* see datasheet note */
//	imxtm->gpt->gpt_setup_tctl(imxtm);

	ret = mxc_clocksource_init(imxtm);
	if (ret)
		return ret;

	mxc_clockevent_init(imxtm);

//	imxtm->gpt->gpt_irq_disable(imxtm);
	imxtm->gpt->gpt_irq_enable(imxtm);


    return 0;

}
#if 0
static int __init rfnm_gpt_init(void)
{
	struct imx_timer *imxtm;

	printk("GPT gpt timer init\n");

	imxtm = kzalloc(sizeof(*imxtm), GFP_KERNEL);
	BUG_ON(!imxtm);

    imxtm->clk_per = clk_get_sys("gpt1", "ipg_clk");
	imxtm->clk_ipg = clk_get_sys("gpt1", "ipg_clk");

	//imxtm->clk_per = clk_get_sys(NULL, "gpt1_root_clk");
	//imxtm->clk_ipg = clk_get_sys(NULL, "gpt1");

	imxtm->base = ioremap(0x302d0000, SZ_4K);
	BUG_ON(!imxtm->base);

	imxtm->type = GPT_TYPE_IMX6DL;
	imxtm->irq = 55;

	_mxc_timer_init(imxtm);

    return 0;
}
#else

struct imx_timer *imxtm_exit;

static int __init rfnm_gpt_init(void)
{
	struct imx_timer *imxtm;
	static int initialized;
	int ret;
    struct device_node *np;

#if 1                                        
                                                                        
    np = of_find_node_by_path("/rfnm_tti");        
    
	if (initialized)
		return 0;

	imxtm = kzalloc(sizeof(*imxtm), GFP_KERNEL);
	if (!imxtm)
		return -ENOMEM;
    imxtm_exit = imxtm;

    void __iomem *gpt_iomem;

	gpt_iomem = ioremap(0x302F0000u, SZ_4K);
	imxtm->base = (volatile unsigned int *) gpt_iomem;
	if (!imxtm->base)
		return -ENXIO;

	imxtm->irq = irq_of_parse_and_map(np, 0);
	if (imxtm->irq <= 0)
		return -EINVAL;

	imxtm->clk_ipg = of_clk_get_by_name(np, "ipg");
    if (IS_ERR(imxtm->clk_ipg))
		printk("clk err\n");

	imxtm->clk_per = of_clk_get_by_name(np, "per");
	if (IS_ERR(imxtm->clk_per))
		printk("clk err\n");

	//imxtm->type = type;

	ret = _mxc_timer_init(imxtm);
	if (ret)
		return ret;

	initialized = 1;

	rfnm_gpio_output(1, RFNM_DGB_GPIO5_16);

#else

	rfnm_gpio_output(1, RFNM_DGB_GPIO5_16);

	void __iomem *gpio_iomem;
	volatile unsigned int *gpio;


	volatile char tmpbuf[100000];
	gpio_iomem = ioremap(0x00900000, SZ_32K);
	gpio = (volatile unsigned int *) gpio_iomem;

	rfnm_gpio_set(1, RFNM_DGB_GPIO5_16);
	rfnm_gpio_set(1, RFNM_DGB_GPIO5_16);
	rfnm_gpio_set(1, RFNM_DGB_GPIO5_16);
	rfnm_gpio_set(1, RFNM_DGB_GPIO5_16);
	rfnm_gpio_set(1, RFNM_DGB_GPIO5_16);

	int i;
	for(i = 0; i < 1000*10; i++) {
		rfnm_gpio_set(1, RFNM_DGB_GPIO5_16);
		memcpy(&tmpbuf[0], gpio, 1024*1);
	}
	

	rfnm_gpio_clear(1, RFNM_DGB_GPIO5_16);








#endif






	return 0;
}

#endif


static __exit void rfnm_gpt_exit(void) {

    free_irq(imxtm_exit->irq, imxtm_exit);  

}


MODULE_PARM_DESC(device, "RFNM GPT Driver");


module_init(rfnm_gpt_init);
module_exit(rfnm_gpt_exit);

MODULE_LICENSE("GPL");
