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



asm_function rfnm_unpack12to16_aarch64

dest .req x0
src .req x1
count .req x2



.text
.align 5
.global rfnm_unpack12to16_aarch64


//PRFM PLDL1STRM,   [src]
//PRFM PLDL1STRM,   [src, #48]
//PRFM PLDL1STRM,   [src, #96]
//PRFM PLDL1STRM,   [src, #144]

.align 5

loop:

ld3     {v20.16b, v21.16b, v22.16b}, [src], #48
ld3     {v23.16b, v24.16b, v25.16b}, [src], #48
ld3     {v26.16b, v27.16b, v28.16b}, [src], #48
ld3     {v29.16b, v30.16b, v31.16b}, [src], #48

cmp     count, #256
beq     sky_prefetch

PRFM PLDL1STRM,   [src]
PRFM PLDL1STRM,   [src, #48]
PRFM PLDL1STRM,   [src, #96]
PRFM PLDL1STRM,   [src, #144]

sky_prefetch:

SHL    v0.16b, v20.16b, #4
USHR   v1.16b, v20.16b, #4
SHL    v2.16b, v21.16b, #4
ORR    v1.16b, v1.16b, v2.16b

USHR   v2.16b, v21.16b, #4
SHL    v2.16b, v2.16b, #4
MOV    v3.16b, v22.16b
 

SHL    v4.16b, v23.16b, #4
USHR   v5.16b, v23.16b, #4
SHL    v6.16b, v24.16b, #4
ORR    v5.16b, v5.16b, v6.16b

USHR   v6.16b, v24.16b, #4
SHL    v6.16b, v6.16b, #4
MOV    v7.16b, v25.16b
 

SHL    v8.16b, v26.16b, #4
USHR   v9.16b, v26.16b, #4
SHL    v10.16b, v27.16b, #4
ORR    v9.16b, v9.16b, v10.16b

USHR   v10.16b, v27.16b, #4
SHL    v10.16b, v10.16b, #4
MOV    v11.16b, v28.16b
 

SHL    v12.16b, v29.16b, #4
USHR   v13.16b, v29.16b, #4
SHL    v14.16b, v30.16b, #4
ORR    v13.16b, v13.16b, v14.16b

USHR   v14.16b, v30.16b, #4
SHL    v14.16b, v14.16b, #4
MOV    v15.16b, v31.16b



// store

st4     {v0.16b, v1.16b, v2.16b, v3.16b}, [dest], #64
st4     {v4.16b, v5.16b, v6.16b, v7.16b}, [dest], #64
st4     {v8.16b, v9.16b, v10.16b, v11.16b}, [dest], #64
st4     {v12.16b, v13.16b, v14.16b, v15.16b}, [dest], #64

subs    count, count, #256

bne     loop




ret

.unreq dest
.unreq src
.unreq count



/*
 
 <?php

 for($i = 0; $i < 4; $i++) {
     
     $sreg = 20 + (3 * $i);
     
     $reg[0] = $sreg + 0;
     $reg[1] = $sreg + 1;
     $reg[2] = $sreg + 2;
     
     $v[0] = ($i*4) + 0;
     $v[1] = ($i*4) + 1;
     $v[2] = ($i*4) + 2;
     $v[3] = ($i*4) + 3;
     
     echo <<<EOF
    
    
    SHL    v{$v[0]}.16b, v{$reg[0]}.16b, #4
    USHR   v{$v[1]}.16b, v{$reg[0]}.16b, #4
    SHL    v{$v[2]}.16b, v{$reg[1]}.16b, #4
    ORR    v{$v[1]}.16b, v{$v[1]}.16b, v{$v[2]}.16b
    
    USHR   v{$v[2]}.16b, v{$reg[1]}.16b, #4
    SHL    v{$v[2]}.16b, v{$v[2]}.16b, #4
    MOV    v{$v[3]}.16b, v{$reg[2]}.16b
 
EOF;
 }
  

 
 */
