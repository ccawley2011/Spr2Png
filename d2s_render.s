        GET     s.ARM32

        AREA    |Asm$$Code|, CODE, READONLY

        IMPORT  malloc
        IMPORT  heap_malloc_asm
        IMPORT  heap_realloc_asm


call_r9 MOV     pc,r9

shunt   LDR     r10,=c_regs
        LDMIA   r10,{r10,r11,r12,pc} ; help the C exception handler

call_artworks
        STMFD   r13!,{r0-r3,r10,r11,r14}
        MOV     r0,#3
        ADR     r1,shunt
        LDR     r2,=c_regs
        STMIA   r2,{r10,r11}
        SWI     &20040 ; XOS_ChangeEnvironment ; my handler :-)
        LDR     r0,=c_regs+8
        STMIA   r0,{r1,r2}
        LDMIA   r13!,{r0-r3}
        BL      call_r9
        MOVVC   r0,#0
        STMFD   r13!,{r0}
        LDR     r0,=c_regs+8
        LDMIA   r0,{r1,r2}
        MOV     r0,#3
        SWI     &20040 ; XOS_ChangeEnvironment ; restore handler
        LDMFD   r13!,{r0}
        TEQ     r0,#0
        SWINE   &2002B ; XOS_GenerateError
        LDMFD   r13!,{r10,r11,pc}


; _kernel_oserror *aw_fileinit(const void *data, size_t *size)
        EXPORT  aw_fileinit
aw_fileinit
        STMFD   r13!,{r1,r4-r11,r14}
        LDR     r4,=varbase
        LDR     r1,[r1]
        STMIA   r4!,{r0,r1}
        MOV     r0,#-1
        STMIA   r4,{r0,r1}
        SWI     &66080 ; XAWRender_FileInitAddress
        LDMVSFD r13!,{r1,r4-r11,pc}$_HAT
        MOV     r9,r0
        MOV     r12,r1
        LDR     r4,=varbase
        LDMIA   r4,{r0,r2}
        ADR     r1,aw_init_callback
        BL      call_artworks
        LDMFD   r13!,{r1}
        LDMVSFD r13!,{r4-r11,pc}$_HAT
        STR     r0,[r1]
        MOV     r5,r0
        MOV     r0,#1
        BL      heap_malloc_asm
        LDR     r4,=varbase
        MOV     r1,#0
        LDR     r2,[r4]
        STMIA   r4,{r0-r2,r5}
        SWI     &66081; XAWRender_RenderAddress
        LDRVC   r4,=awrender_code
        STMVCIA r4,{r0,r1}
        MOVVC   r0,#0
        LDMFD   r13!,{r4-r11,pc}$_HAT

aw_init_callback
        STMFD   r13!,{r14}
        CMP     r11,#1
        BLO     aw_init_memman
        ADDS    r0,r0,#0 ; clc
        LDMFD   r13!,{pc}
aw_init_memman
        CMP     r0,#-1
        CMPNE   r0,#0
        LDREQ   r0,=varbase
        LDMEQIA r0,{r0-r3}
        LDMEQFD r13!,{pc}
        ADR     r0,aw_init_fail
        SWI     &2002B ; XOS_GenerateError
        LDMFD   r13!,{pc}
aw_init_fail
        &       256
        =       "unexpected memory management call",0
        ALIGN


; _kernel_oserror *render(void *area, void *drawfile, size_t size,
;                         draw_matrix *matrix, int type, int thicken,
;                         const char *vdu, size_t vdusize)
        EXPORT  render
render  ROUT
        MOV     r12,r13
        STMFD   r13!,{r0-r11,lr}
        LDR     r4,=save
        LDR     r2,[r4]
        TEQ     r2,#0
        BEQ     gotsavearea
        MOV     r0,#62
        MOV     r1,#0
        MOV     r2,#0
        SWI     &2002E ; XOS_SpriteOp
        BVS     errorret
        MOV     r0,r2
        BL      malloc
        TEQ     r0,#0
        ADREQ   r0,mallocfail
        BEQ     errorret
        STR     r0,[r4]
        MOV     r1,#0
        STR     r1,[r0]
gotsavearea
        MOV     r8,r13
        MOV     r0,#0
        SWI     &606C0 ; XHourglass_On
        MOV     r0,#1
        MOV     r1,#-1
        SWI     &606C5 ; XHourglass_LEDs
        MOV     r0,#60+256
        LDR     r1,[r8]
        ADD     r2,r1,#20
        LDR     r3,=save
        LDR     r3,[r3]
        SWI     &2002E ; XOS_SpriteOp
        BVS     errorret_svc
        LDR     r5,=save_context
        STMIA   r5,{r0-r3}
        ADD     r0,r12,#8
        LDMIA   r0,{r0,r1} ; VDU data (clip rectangle etc.)
        TEQ     r0,#0
        SWINE   &20046 ; XOS_WriteN
        LDR     r0,[r12,#4]
        TEQ     r0,#0
        BNE     nofix
        MOV     r0,#&20 ; DrawV
        ADR     r1,fiddle_draw
        MOV     r2,#0
        SWI     &2001F ; XOS_Claim
        BVS     errorret_svc
nofix   LDR     r0,[r12]
        TEQ     r0,#0
        BNE     awrender
        ; do Drawfile rendering
        MOV     r0,#0
        LDMIB   r8,{r1-r3}
        MOV     r4,#0
        SWI     &65540 ; XDrawfile_Render
        MOVVC   r0,#0
        B       errorret_svc
awrender
        ; do Artworks rendering
        LDR     r4,=varbase
        SUB     r6,r0,#1
        LDR     r0,[r4,#8] ; fixbase
        LDR     r1,=infoblock
        LDR     r2,=tm
        LDR     r3,=vduvars
        LDR     r4,[r4] ; varbase
        ADR     r5,awrender_callback
        MOV     r7,#0
        LDR     r9,=awrender_code
        LDMIA   r9,{r9,r12}
        BL      call_artworks
        MOVVC   r0,#0
errorret_svc
        BL      render_shutdown
        MOV     r4,r0
        MOV     r0,#1
        MOV     r1,#-1
        SWI     &606C5 ; XHourglass_LEDs
        MOV     r0,#0
        SWI     &606C1 ; XHourglass_Off
        MOV     r0,r4
errorret
        ADD     r13,r13,#4
        LDMFD   r13!,{r1-r11,pc}$_HAT

mallocfail
        &       0
        =       "out of memory (vector graphics render)",0
        ALIGN

awrender_callback
        STMFD   r13!,{r14}
        CMP     r11,#1
        BEQ     awrender_clc
        LDMHIFD r13!,{pc}
        CMP     r0,#-1
        CMPNE   r0,#0
        BEQ     awrender_return
        STMFD   r13!,{r4-r11}
        LDR     r4,=varbase
        LDMIA   r4,{r5,r6}
        ADDS    r1,r6,r0
        MOV     r0,r5
        STR     r1,[r4,#4]
        MOVEQ   r1,#1
;       ADD     r1,r1,#4096
;       SUB     r1,r1,#1
;       MOV     r1,r1,LSR #12
;       MOV     r1,r1,LSL #12 ; round up to multiple of 4096
        BL      heap_realloc_asm
        TEQ     r0,#0
        STRNE   r0,[r4]
        ADREQ   r0,mallocfail
        SWIEQ   &2002B ; XOS_GenerateError
        LDMFD   r13!,{r4-r11}
        LDMVSFD r13!,{pc}
awrender_return
        LDR     r0,=varbase
        LDMIA   r0,{r0-r3}
awrender_clc
        ADDS    r0,r0,#0 ; clv
        LDMFD   r13!,{pc}

fiddle_draw
        TEQ     r8,#4
        TEQEQ   r4,#0
        MOVSPC  lr,NE
        EOR     r1,r1,#&18
        TST     r1,#&3C
        EOREQ   r1,r1,#&30
        EORNE   r1,r1,#&18
        TEQ     r5,#0
        ADREQ   r5,joincap
        MOV     r4,#130*4;204*4
        MOVSPC  lr
joincap =       0,0,0,0
        &       &10000
        =       0,0, 0,0, 0,0, 0,0

        EXPORT  render_shutdown
render_shutdown ROUT
        STMFD   sp!,{r0,lr} ; must preserve R0
        MOV     r0,#&20 ; DrawV
        ADR     r1,fiddle_draw
        MOV     r2,#0
        SWI     &20020 ; XOS_Release
        LDR     ip,=save_context
        LDMIA   ip,{r0-r3}
        TEQ     r0,#0
        MOVNE   lr,#0
        STRNE   lr,[ip]
        SWINE   &2002E ; XOS_SpriteOp
        LDMFD   sp!,{r0,pc}$_HAT


; void makemask (sprite_t *sprite, long width, long height, long colour)
        EXPORT  makemask
makemask
        MOV     ip,sp
        STMFD   sp!,{r4-r7,fp,ip,lr,pc}
        SUB     fp,ip,#4
        LDR     r3,[r0,#32]
        LDR     r4,[r0,#36]
        ADD     ip,r0,r3        ; image
        ADD     r0,r0,r4        ; mask
mx      MOVS    r3,r1,LSR #3
        BEQ     mx7
mxL     MOV     lr,#255         ; do 8 pixels at a time
        LDMIA   ip!,{r4-r7}
        TST     r4,#&FF<<24
        BICEQ   lr,lr,#1
        TST     r5,#&FF<<24
        BICEQ   lr,lr,#2
        TST     r6,#&FF<<24
        BICEQ   lr,lr,#4
        TST     r7,#&FF<<24
        BICEQ   lr,lr,#8
        LDMIA   ip!,{r4-r7}
        TST     r4,#&FF<<24
        BICEQ   lr,lr,#16
        TST     r5,#&FF<<24
        BICEQ   lr,lr,#32
        TST     r6,#&FF<<24
        BICEQ   lr,lr,#64
        TST     r7,#&FF<<24
        BICEQ   lr,lr,#128
        STRB    lr,[r0],#1
        SUBS    r3,r3,#1
        BNE     mxL
mx7     ANDS    r3,r1,#7
        BEQ     mx0
        MOV     lr,#255
        MOV     r4,#1
mxS     LDR     r5,[ip],#4      ; do remaining pixels
        MOV     r4,r4,LSL #1;SA
        TST     r5,#&FF<<24
        BICEQ   lr,lr,r4,LSR #1
        SUBS    r3,r3,#1
        BNE     mxS
        STRB    lr,[r0],#1
mx0     ADD     r0,r0,#3        ; round up mask ptr
        BIC     r0,r0,#3
        SUBS    r2,r2,#1
        BNE     mx
        LDMDB   fp,{r4-r7,fp,sp,pc}$_HAT


        AREA    |Asm$$Data|, DATA

vduvars &       1, 1, 5
        %       20*4
        EXPORT  vduvars

tm      &       &40000,0,0,&40000,0,0
        EXPORT  tm


        AREA    |Asm$$ZIData|, DATA, NOINIT

c_regs          &       0,0,0,0

save            &       0
save_context    &       0,0,0,0

awrender_code   &       0,0

varbase         &       0
varsize         &       0
fixbase         &       0
fixsize         &       0

box             %       16
        EXPORT  box

infoblock       %       36
        EXPORT  infoblock

        END
