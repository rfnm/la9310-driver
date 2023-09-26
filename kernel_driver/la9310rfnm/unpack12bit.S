/*
*   void fanic_unpack12to16(unsigned short *pDst, unsigned short *pSrc, unsigned int count);
*   assert :
*       count >=64
*       count % 4 == 0
*   
*   written by : Jake Lee
*   part of FANIC project - Fastest ARM NEON Implementation Challenge
*/

/*
    pDst .req r0
    pSrc .req r1
    count .req r2

    .text
    .arm
    .global fanic_unpack12to16:

    .func
    .align 5
fanic_unpack12to16:

    pld [pSrc]
    pld [pSrc, #64*1]
    pld [pSrc, #64*2]
    vpush       {q4}
    pld [pSrc, #64*3]
    vmov.i16    q4, #0x0fff
    pld [pSrc, #64*4]
    sub count, count, #64

    .align 5
1:
    vld3.16     {d20, d22, d24}, [pSrc]!
    vld3.16     {d21, d23, d25}, [pSrc]!
    vld3.16     {d26, d28, d30}, [pSrc]!
    vld3.16     {d27, d29, d31}, [pSrc]!
    pld     [pSrc, #128]
    pld     [pSrc, #192]
    subs    count, count, #64

    vshr.u16    q1, q11, #8
    vshr.u16    q2, q12, #12
    vshr.u16    q0, q10, #4
    vand        q3, q12, q4
        vshr.u16    q9, q14, #8
    vsli.16     q1, q10, #8
    vsli.16     q2, q11, #4
        vshr.u16    q10, q15, #12
        vsli.16     q9, q13, #8
    vbic.i16    q1, q1, #0xf000
    vbic.i16    q2, q2, #0xf000
    vsli.16     q10, q14, #4
    vshr.u16    q8, q13, #4
    vbic.i16    q9, q9, #0xf000
    vand        q11, q15, q4
    vbic.i16    q10, q10, #0xf000

    vst4.16     {d0, d2, d4, d6}, [pDst]!
    vst4.16     {d1, d3, d5, d7}, [pDst]!
    vst4.16     {d16, d18, d20, d22}, [pDst]!
    vst4.16     {d17, d19, d21, d23}, [pDst]!
    bpl     1b

    cmp     count, #-64
    add     pSrc, pSrc, count

    vpople      {q4}
    bxle    lr

    add     pSrc, pSrc, count, asr #1
    add     pDst, pDst, count, lsl #1
    b       1b

    .endfunc
    .end

    */