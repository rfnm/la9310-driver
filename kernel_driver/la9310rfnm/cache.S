#include <linux/linkage.h>



	.macro __dcache_op_workaround_clean_cache, op, addr
//alternative_if_not ARM64_WORKAROUND_CLEAN_CACHE
	dc	\op, \addr
//alternative_else
//	dc	civac, \addr
//alternative_endif
	.endm

	.macro dcache_by_myline_op op, domain, start, end, linesz, tmp, fixup
	sub	\tmp, \linesz, #1
	bic	\start, \start, \tmp
.Ldcache_op\@:
	.ifc	\op, cvau
	__dcache_op_workaround_clean_cache \op, \start
	.else
	.ifc	\op, cvac
	__dcache_op_workaround_clean_cache \op, \start
	.else
	.ifc	\op, cvap
	sys	3, c7, c12, 1, \start	// dc cvap
	.else
	.ifc	\op, cvadp
	sys	3, c7, c13, 1, \start	// dc cvadp
	.else
	dc	\op, \start
	.endif
	.endif
	.endif
	.endif
	add	\start, \start, \linesz
	cmp	\start, \end
	b.lo	.Ldcache_op\@
	dsb	\domain

	//_cond_uaccess_extable .Ldcache_op\@, \fixup // <-- lol
	.endm

	.macro dcache_by_line_op op, domain, start, end, tmp1, tmp2, fixup
	dcache_line_size \tmp1, \tmp2
	dcache_by_myline_op \op, \domain, \start, \end, \tmp1, \tmp2, \fixup
	.endm





/*
 *      dcache_inval_poc(start, end)
 *
 *      Ensure that any D-cache lines for the interval [start, end)
 *      are invalidated. Any partial lines at the ends of the interval are
 *      also cleaned to PoC to prevent data loss.
 *
 *      - start   - kernel start address of region
 *      - end     - kernel end address of region
 */

	.macro	dcache_line_size, reg, tmp
	read_ctr	\tmp
	ubfm		\tmp, \tmp, #16, #19	// cache line size encoding
	mov		\reg, #4		// bytes per word
	lsl		\reg, \reg, \tmp	// actual cache line size
	.endm

    	.macro	read_ctr, reg
mrs	\reg, ctr_el0			// read CTR
	nop
	.endm


SYM_FUNC_START_LOCAL(__dma_inv_area)
SYM_FUNC_START_PI(dcache_inval_poc)
        /* FALLTHROUGH */

/*
 *      __dma_inv_area(start, end)
 *      - start   - virtual start address of region
 *      - end     - virtual end address of region
 */
        dcache_line_size x2, x3
        sub     x3, x2, #1
        tst     x1, x3                          // end cache line aligned?
        bic     x1, x1, x3
        b.eq    1f
        dc      civac, x1                       // clean & invalidate D / U line
1:      tst     x0, x3                          // start cache line aligned?
        bic     x0, x0, x3
        b.eq    2f
        dc      civac, x0                       // clean & invalidate D / U line
        b       3f
2:      dc      ivac, x0                        // invalidate D / U line
3:      add     x0, x0, x2
        cmp     x0, x1
        b.lo    2b
        dsb     sy
        ret
SYM_FUNC_END_PI(dcache_inval_poc)
SYM_FUNC_END(__dma_inv_area)





/*
 *	dcache_clean_poc(start, end)
 *
 * 	Ensure that any D-cache lines for the interval [start, end)
 * 	are cleaned to the PoC.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
SYM_FUNC_START_LOCAL(__dma_clean_area)
SYM_FUNC_START_PI(dcache_clean_poc)
	/* FALLTHROUGH */

/*
 *	__dma_clean_area(start, end)
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
	dcache_by_line_op cvac, sy, x0, x1, x2, x3
	ret
SYM_FUNC_END_PI(dcache_clean_poc)
SYM_FUNC_END(__dma_clean_area)



