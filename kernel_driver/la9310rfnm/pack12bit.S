/*
*   void fanic_pack16to12(unsigned short * pDst, unsigned short * pSrc, unsigned int count);
*   assert :
*       count >= 64
*       count % 4 == 0
*
*   written by : Jake Lee
*   part of FANIC project - Fastest ARM NEON Implementation Challenge
*/
    pDst .req w0
    pSrc .req w1
    count .req w2

    .text
    .cpu cortex-a53
    .global fanic_pack16to12

    .type	fanic_pack16to12, %function
    .align 5
fanic_pack16to12:
    pld     [pSrc]
    pld     [pSrc, #64]
    pld     [pSrc, #128]
    pld     [pSrc, #192]
    pld     [pSrc, #256]
    sub     count, count, #64

    .align 5
1:
    vld4.16     {d16, d18, d20, d22}, [pSrc]!
    vld4.16     {d17, d19, d21, d23}, [pSrc]!
    vld4.16     {d24, d26, d28, d30}, [pSrc]!
    vld4.16     {d25, d27, d29, d31}, [pSrc]!
    pld     [pSrc, #128]
    pld     [pSrc, #192]
    subs    count, count, #64

    vqshl.u16   q0, q8, #4
    vqshl.u16   q3, q9, #4
    vqshl.u16   q8, q10, #4
    vqshl.u16   q9, q11, #4
        vqshl.u16   q10, q12, #4
        vqshl.u16   q13, q13, #4
        vqshl.u16   q14, q14, #4
        vqshl.u16   q15, q15, #4
    vshl.u16    q1, q3, #4
    vshl.u16    q2, q8, #8
        vshl.u16    q11, q13, #4
        vshl.u16    q12, q14, #8
    vsri.16     q0, q3, #12
    vsri.16     q1, q8, #8
    vsri.16     q2, q9, #4
        vsri.16     q10, q13, #12
        vsri.16     q11, q14, #8
        vsri.16     q12, q15, #4

    vst3.16     {d0, d2, d4}, [pDst]!
    vst3.16     {d1, d3, d5}, [pDst]!
    vst3.16     {d20, d22, d24}, [pDst]!
    vst3.16     {d21, d23, d25}, [pDst]!
    bpl     1b

    cmp     count, #-64
    add     pDst, pDst, count

    bxle    lr

    add     pSrc, pSrc, count, lsl #1
    add     pDst, pDst, count, asr #1
    b       1b
     .endfunc
     .end