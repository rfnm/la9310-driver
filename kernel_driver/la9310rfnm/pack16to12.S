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

dest .req x0
src .req x1
count .req x2



.text
.align 5
.global rfnm_pack16to12_aarch64

//PRFM PLDL1STRM,   [src]
//PRFM PLDL1STRM,   [src, #64]
//PRFM PLDL1STRM,   [src, #128]
//PRFM PLDL1STRM,   [src, #192]

.align 5

loop:

ld4     {v16.16b, v17.16b, v18.16b, v19.16b}, [src], #64
ld4     {v20.16b, v21.16b, v22.16b, v23.16b}, [src], #64
ld4     {v24.16b, v25.16b, v26.16b, v27.16b}, [src], #64
ld4     {v28.16b, v29.16b, v30.16b, v31.16b}, [src], #64

cmp     count, #256
beq     sky_prefetch

PRFM PLDL1STRM,   [src]
PRFM PLDL1STRM,   [src, #64]
PRFM PLDL1STRM,   [src, #128]
PRFM PLDL1STRM,   [src, #192]

sky_prefetch:

USHR   v0.16b, v16.16b, #4
SHL    v2.16b, v17.16b, #4
ORR    v0.16b, v0.16b, v2.16b

USHR   v1.16b, v17.16b, #4
USHR   v2.16b, v18.16b, #4
SHL    v2.16b, v2.16b, #4
ORR    v1.16b, v1.16b, v2.16b

MOV    v2.16b, v19.16b
    
USHR   v3.16b, v20.16b, #4
SHL    v5.16b, v21.16b, #4
ORR    v3.16b, v3.16b, v5.16b

USHR   v4.16b, v21.16b, #4
USHR   v5.16b, v22.16b, #4
SHL    v5.16b, v5.16b, #4
ORR    v4.16b, v4.16b, v5.16b

MOV    v5.16b, v23.16b
    
USHR   v6.16b, v24.16b, #4
SHL    v8.16b, v25.16b, #4
ORR    v6.16b, v6.16b, v8.16b

USHR   v7.16b, v25.16b, #4
USHR   v8.16b, v26.16b, #4
SHL    v8.16b, v8.16b, #4
ORR    v7.16b, v7.16b, v8.16b

MOV    v8.16b, v27.16b
    
USHR   v9.16b, v28.16b, #4
SHL    v11.16b, v29.16b, #4
ORR    v9.16b, v9.16b, v11.16b

USHR   v10.16b, v29.16b, #4
USHR   v11.16b, v30.16b, #4
SHL    v11.16b, v11.16b, #4
ORR    v10.16b, v10.16b, v11.16b

MOV    v11.16b, v31.16b








// store

st3     {v0.16b, v1.16b, v2.16b}, [dest], #48
st3     {v3.16b, v4.16b, v5.16b}, [dest], #48
st3     {v6.16b, v7.16b, v8.16b}, [dest], #48
st3     {v9.16b, v10.16b, v11.16b}, [dest], #48

subs    count, count, #256

bne     loop




ret

.unreq dest
.unreq src
.unreq count



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
  

 
 
 */
