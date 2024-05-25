/* Supplementary macro for setting function attributes */
.macro asm_function fname
#ifdef __APPLE__
    .private_extern _\fname
    .globl _\fname
_\fname:
#else
    .global \fname
#ifdef __ELF__
    .hidden \fname
    .type \fname, %function
#endif
\fname:
#endif
.endm



asm_function rfnm_pack16to12_aarch64
        cmp     x2, 3
        bls     .L1
        movi    v25.8h, 0x80, lsl 8
        sub     x3, x2, #4
        movi    v24.16b, 0xf
        lsr     x3, x3, 2
        add     x3, x3, 1
        lsr     x3, x3, 4
        add     x3, x1, x3, lsl 6
.L3:
        mov     x2, x1
        add     x1, x1, 64
        cmp     x3, x1
        ld2     {v26.8h - v27.8h}, [x2], 32
        ld2     {v30.8h - v31.8h}, [x2]
        and     v5.16b, v25.16b, v26.16b
        cmlt    v1.8h, v26.8h, #0
        and     v4.16b, v25.16b, v30.16b
        and     v3.16b, v25.16b, v27.16b
        sub     v5.8h, v5.8h, v26.8h
        cmlt    v0.8h, v30.8h, #0
        sub     v4.8h, v4.8h, v30.8h
        cmlt    v23.8h, v27.8h, #0
        bsl     v1.16b, v5.16b, v26.16b
        sub     v3.8h, v3.8h, v27.8h
        and     v2.16b, v25.16b, v31.16b
        bsl     v0.16b, v4.16b, v30.16b
        shrn    v28.8b, v1.8h, 4
        bsl     v23.16b, v3.16b, v27.16b
        sub     v2.8h, v2.8h, v31.8h
        cmlt    v30.8h, v31.8h, #0
        shrn2   v28.16b, v0.8h, 4
        sshr    v1.8h, v1.8h, 12
        shrn    v29.8b, v23.8h, 4
        bsl     v30.16b, v2.16b, v31.16b
        sshr    v0.8h, v0.8h, 12
        shrn2   v29.16b, v30.8h, 4
        uzp1    v0.16b, v1.16b, v0.16b
        uzp2    v30.16b, v23.16b, v30.16b
        shl     v29.16b, v29.16b, 4
        and     v0.16b, v0.16b, v24.16b
        orr     v29.16b, v0.16b, v29.16b
        st3     {v28.16b - v30.16b}, [x0], 48
        bne     .L3
.L1:
        ret



/*
 
 <?php

 for($i = 0; $i < 4; $i++) {
     
     $sreg = 16 + (4 * $i);
     
     $reg[0] = $sreg + 0;
     $reg[1] = $sreg + 1;
     $reg[2] = $sreg + 2;
     $reg[3] = $sreg + 3;
     
     $v[0] = ($i*3) + 0;
     $v[1] = ($i*3) + 1;
     $v[2] = ($i*3) + 2;
     
     echo <<<EOF
     
     USHR   v{$v[0]}.16b, v{$reg[0]}.16b, #4
     SHL    v{$v[2]}.16b, v{$reg[1]}.16b, #4
     ORR    v{$v[0]}.16b, v{$v[0]}.16b, v{$v[2]}.16b
     
     USHR   v{$v[1]}.16b, v{$reg[1]}.16b, #4
     USHR   v{$v[2]}.16b, v{$reg[2]}.16b, #4
     SHL    v{$v[2]}.16b, v{$v[2]}.16b, #4
     ORR    v{$v[1]}.16b, v{$v[1]}.16b, v{$v[2]}.16b
     
     MOV    v{$v[2]}.16b, v{$reg[3]}.16b
     
 EOF;
 }

 
 
 
 <?php
 
 echo <<<EOF
 
ld1     {v0.8h, v1.8h, v2.8h, v3.8h}, [src]
      
EOF;
  
  
 for($i = 0; $i < 4; $i++) {
     
     $sreg = 0 + (4 * $i);
     
     $SIGN = $sreg + 0;
     $SOURCE = $i;
     $DEST = $sreg + 1;
     $VAL = $sreg + 2;
     $SUB = $sreg + 3;
     //$SUB = $SOURCE;
     
     echo <<<EOF
    
     USHR   v{$SIGN}.8h, v{$SOURCE}.8h, #15 // sign bit only
     MOVI   v{$DEST}.16b, #255
     MUL    v{$DEST}.8h, v{$SIGN}.8h, v{$DEST}.8h
     
     SHL   v{$VAL}.8h, v{$SOURCE}.8h, #1 // remove sign bit
     USHR   v{$VAL}.8h, v{$VAL}.8h, #1
     
     MOVI   v{$SUB}.8h, #128
     SHL   v{$SUB}.8h, v{$SUB}.8h, #8
     
     SUB v{$SUB}.8h, v{$SUB}.8h, v{$VAL}.8h
     BSL v{$DEST}.16b, v{$SUB}.16b, v{$VAL}.16b
     
     SHL v{$SIGN}.8h, v{$SIGN}.8h, #15
     ORR v{$SOURCE}.16b, v{$DEST}.16b, v{$SIGN}.16b
     
     
     
     
     
     
EOF;

 }
 
 
 echo <<<EOF
  
 st1     {v0.8h, v1.8h, v2.8h, v3.8h}, [src]
       
       
EOF;
  
 
 
 




#include <stddef.h>
#include <stdint.h>
#include <arm_neon.h>

static inline int16_t sign_magnitude_to_i16(int16_t x) {
    int16_t m = x >> 15;
    return (~m & x) | (((x & 0x8000u) - x) & m);
}

void autovectorized (uint8_t * restrict dst, int16_t * restrict src, size_t byte_count)
{
    if (byte_count & 255)
        __builtin_unreachable();

    while (byte_count >= 4) {
        int16_t i = (*src++) >> 4;
        int16_t q = (*src++) >> 4;
        *dst++ = i & 0xFF;
        *dst++ = ((i >> 8) & 0xF) | ((q << 4) & 0xF0);
        *dst++ = (q >> 4) & 0xFF;
        byte_count -= 4;
    } 
}

// void neon_convert (int16_t *src, uint8_t *dst, size_t byte_count)
// {
//     while (byte_count >= 256) {
//         for (size_t i=0; i < 8; i++) {
//             int16x8x2_t unpacked1 = vld2q_s16(src);
//             int16x8x2_t m;
//             m.val[0] = unpacked1.val[0] >> 15;
//             m.val[1] = unpacked1.val[1] >> 15;
//             int16x8x2_t twos_complement;
//             twos_complement.val[0] = ((~m.val[0] & unpacked1.val[0]) | (((unpacked1.val[0] & 0x8000u) - unpacked1.val[0]) & unpacked1.val[0])) >> 4;
//             twos_complement.val[1] = ((~m.val[1] & unpacked1.val[1]) | (((unpacked1.val[1] & 0x8000u) - unpacked1.val[1]) & unpacked1.val[1])) >> 4;
//             uint8x8x3_t packed1;
//             packed1.val[0] = vmovn_u16(vreinterpretq_u16_s16(twos_complement.val[0]));
//             packed1.val[1] = (vmovn_u16(vreinterpretq_u16_s16(twos_complement.val[0] >> 8)) & 0xF) | (vmovn_u16(vreinterpretq_u16_s16(twos_complement.val[1])) << 4);
//             packed1.val[2] = vmovn_u16(vreinterpretq_u16_s16(twos_complement.val[1] >> 4));
//             vst3_u8(dst, packed1);
//             src += (8*2);
//             dst += (12);
//         }
//         byte_count -= 256;
//     }
// }





// clang


static inline int16_t sign_magnitude_to_i16(int16_t x) {
    int16_t m = x >> 15;
    return (~m & x) | (((x & 0x8000u) - x) & m);
}

void autovectorized (uint8_t * restrict dst, int16_t * restrict src, size_t byte_count)
{
    if (byte_count & 63)
        __builtin_unreachable();

    for (size_t samples_copied = 0; samples_copied < byte_count/4; samples_copied++) {
        int16_t i = sign_magnitude_to_i16(src[samples_copied*2]) >> 4;
        int16_t q = sign_magnitude_to_i16(src[samples_copied*2+1]) >> 4;
        dst[samples_copied*3] = i & 0xFF;
        dst[samples_copied*3+1] = ((i >> 8) & 0xF) | ((q << 4) & 0xF0);
        dst[samples_copied*3+2] = (q >> 4) & 0xFF;
    } 
}



 
 
 */



