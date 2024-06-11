
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/usb/composite.h>
#include <linux/err.h>

void kernel_neon_begin(void);
void kernel_neon_end(void);

void rfnm_pack16to12_aarch64(uint8_t * dest, uint8_t * src, uint32_t bytes);
void rfnm_unpack12to16_aarch64(uint8_t * dest, uint8_t * src, uint32_t bytes);

void rfnm_pack16to12_aarch64_wrapper(uint8_t * dest, uint8_t * src, uint32_t bytes) {
	kernel_neon_begin();
	rfnm_pack16to12_aarch64(dest, src, bytes);
	kernel_neon_end();
}

void rfnm_unpack12to16_aarch64_wrapper(uint8_t * dest, uint8_t * src, uint32_t bytes) {
	kernel_neon_begin();
	rfnm_unpack12to16_aarch64( dest, src, bytes);
	kernel_neon_end();
}






