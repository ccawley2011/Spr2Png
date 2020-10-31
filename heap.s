	GET	s.ARM32

XOS_WriteC		* &20000

XOS_Heap		* &2001D
XOS_DynamicArea		* &20066
XOS_ReadDynamicArea	* &2005C
XOS_ChangeDynamicArea	* &2002A

dograb	*	0


	AREA	|Asm$$Code|, CODE, READONLY

	IMPORT	atexit
	IMPORT	malloc
	IMPORT	realloc
	IMPORT	free

 [ dograb<>0
grab	ROUT
	STMFD	sp!,{lr}
	LDR	r2,[r0]		; default routine address
	STR	r1,[r0]		; replacement
	SUB	r1,r1,r2
	SUB	r1,r1,#8
	MOV	r1,r1,LSL #6
	MOV	r1,r1,LSR #8
	ORR	r1,r1,#&EA000000
	STR	r1,[r2]		; write a B instruction
	MOV	r0,#1
	MOV	r1,r2
	ADD	r2,r2,#4
	SWI	&2006E		; XOS_SynchroniseCodeAreas
	LDMFD	sp!,{pc}
 ]

; int heap_init(const char *name)
; 0 if successful
	EXPORT heap_init
heap_init ROUT
	MOV	ip,sp
	STMFD	sp!,{r0,r4-r8,fp,ip,lr,pc}
	SUB	fp,ip,#4
	MOV	r0,#2		; Checking the location of the modules area
	MOV	r1,#1		; - if it's above 0x02100000, don't init heap
	SWI	XOS_DynamicArea	; since we have a large application space
	CMP	r3,#&02100000
	MOVHI	r0,#0
	LDMHIDB	fp,{r3-r8,fp,sp,pc}$_HAT
	MOV	r0,#0
	MOV	r1,#-1
	MOV	r2,#4096
	MOV	r3,#-1
	MOV	r4,#1<<7 :OR: 1<<11 ; not user-resizeable; bound to client
	ORR	r4,r4,#1<<31 ; virtualiseable
	MOV	r5,#32*1024*1024 ; 32MB
	MOV	r6,#0
	MOV	r7,#0
	LDR	r8,[sp]
	SWI	XOS_DynamicArea
	LDMVSDB	fp,{r3-r8,fp,sp,pc}$_HAT
	LDR	r0,=area_handle
	STMIA	r0,{r1,r3,r5}
	ADR	r0,heap_dispose
	BL	atexit
	TEQ	r0,#0
	BNE	heap_fail
	LDR	r0,=area_handle
	LDR	r0,[r0]
	SWI	XOS_ReadDynamicArea
	BVS	heap_fail
	MOV	r3,r1
	LDR	r1,=area_ptr
	MOV	r0,#0
	LDR	r1,[r1]
	SWI	XOS_Heap	; initialise heap
	BVS	heap_fail
 [ dograb<>0
	LDR	r0,=heap_malloc
	ADR	r1,internal_heap_malloc
	BL	grab
	LDR	r0,=heap_realloc
	ADR	r1,internal_heap_realloc
	BL	grab
	LDR	r0,=heap_free
	ADR	r1,internal_heap_free
	BL	grab
 |
	LDR	r0,=heap_malloc
	ADR	r1,internal_heap_malloc
	ADR	r2,internal_heap_realloc
	ADR	r3,internal_heap_free
	STMIA	r0,{r1-r3}
 ]
	MOV	r0,#0
	LDMDB	fp,{r3-r8,fp,sp,pc}$_HAT
heap_fail
	BL	heap_dispose
	MOV	r0,#1
	LDMDB	fp,{r3-r8,fp,sp,pc}$_HAT


heap_dispose
	MOV	ip,sp
	STMFD	sp!,{fp,ip,lr,pc}
	SUB	fp,ip,#4
	LDR	ip,=area_handle
	MOV	r0,#1
	LDR	r1,[ip]
	MOV	r2,#0
	TEQ	r1,#0
	STRNE	r2,[ip]
	SWINE	XOS_DynamicArea
	LDMDB	fp,{fp,sp,pc}$_HAT


internal_heap_free
	MOV	ip,sp
	STMFD	sp!,{fp,ip,lr,pc}
	SUB	fp,ip,#4
	SUBS	r2,r0,#0
	LDRNE	r1,=area_ptr
	MOVNE	r0,#3
	LDRNE	r1,[r1]
	SWINE	XOS_Heap	; release block
	LDMVSDB	fp,{fp,sp,pc}$_HAT
	LDR	r1,=area_ptr
	LDR	r1,[r1]
	LDR	r3,[r1,#8]	; heap base ptr (all free from here to end)
	BL	set_da_size
	LDMDB	fp,{fp,sp,pc}$_HAT


internal_heap_malloc
	MOV	ip,sp
	STMFD	sp!,{r0,fp,ip,lr,pc}
	SUB	fp,ip,#4
	BL	heap_malloc_asm
	LDMDB	fp,{fp,sp,pc}$_HAT

heap_malloc_asm
	STMFD	sp!,{r0-r3,r14}
	MOV	r3,r0
	LDR	r1,=area_ptr
	MOV	r0,#2
	LDR	r1,[r1]
	SWI	XOS_Heap	; claim heap block
	BVC	malloc_ok	; return if successful
	LDR	r3,[sp]
	BL	heap_extend	; extend heap
	LDR	r3,[sp]
	LDR	r1,=area_ptr
	MOV	r0,#2
	LDR	r1,[r1]
	SWI	XOS_Heap	; try again to claim heap block
	MOVVS	r2,#0
malloc_ok
	MOV	r0,r2
	ADD	sp,sp,#4
	LDMFD	sp!,{r1-r3,pc}


internal_heap_realloc
	MOV	ip,sp
	STMFD	sp!,{fp,ip,lr,pc}
	SUB	fp,ip,#4
	BL	heap_realloc_asm
	LDMDB	fp,{fp,sp,pc}$_HAT

heap_realloc_asm
	TEQ	r0,#0
	MOVEQ	r0,r1
	BEQ	heap_malloc_asm
	STMFD	sp!,{r0,r1,r3,lr}
	MOV	r2,r0		; heap block ptr
	LDR	r1,=area_ptr
	MOV	r0,#6
	LDR	r1,[r1]		; heap ptr
	SWI	XOS_Heap	; R3 = block size
	SUB	r3,r3,#4	; allow for OS_Heap overheads
	LDR	r2,[sp,#4]	; required size
	MOV	r0,#4
	SUB	r3,r2,r3	; size change
	LDR	r2,[sp]
	STMFD	sp!,{r3}	; store for later
	SWI	XOS_Heap	; resize heap block
	ADDVC	sp,sp,#4
	BVC	realloc_ok	; return if successful
	LDR	r3,[sp,#12]	; required size
	BL	heap_extend	; extend heap
	LDR	r1,=area_ptr
	MOV	r0,#4
	LDR	r1,[r1]		; heap ptr
	LDMFD	sp!,{r3}	; size change
	LDR	r2,[sp]		; heap block ptr
	SWI	XOS_Heap	; try again to resize heap block
	MOVVS	r2,#0
realloc_ok
	CMP	r2,#-1		; R2 == -1 => block has been freed
	MOVEQ	r0,#0
	MOVNE	r0,r2
	ADD	sp,sp,#4
	LDMFD	sp!,{r1,r3,pc}$_HAT


heap_extend ; r3 = required free space; returns VS if failed
	STMFD	sp!,{r0-r3,lr}
	ADD	r3,r3,#4
	LDR	r2,=area_ptr
	LDR	r2,[r2]
	LDR	r2,[r2,#12]	; heap size
	ADD	r3,r2,r3
	BL	set_da_size
	LDMFD	sp!,{r0-r3,pc}


set_da_size
	STMFD	sp!,{r3-r5,lr}
	LDR	r4,=area_handle
	LDR	r0,[r4]		; dynamic area
	SWI	XOS_ReadDynamicArea ; R1 = size
	MOV	r5,r1
	LDR	r0,[r4]
	SUB	r1,r3,r1
	SWI	XOS_ChangeDynamicArea
	LDR	r0,[r4]
	SWI	XOS_ReadDynamicArea ; R1 = size
	SUBS	r3,r1,r5	; size change
	MOVLT	r3,#0
	MOVNE	r0,#5
	LDRNE	r1,=area_ptr
	LDRNE	r1,[r1]
	SWINE	XOS_Heap
	LDMFD	sp!,{r3-r5,pc}


	; For use by rendering code
	EXPORT	heap_malloc_asm
	EXPORT	heap_realloc_asm


	AREA	|Asm$$Data|, DATA
; Preserve this layout!

	EXPORT	heap_malloc
heap_malloc	&	malloc
	EXPORT	heap_realloc
heap_realloc	&	realloc
	EXPORT	heap_free
heap_free	&	free

area_handle	&	0
area_ptr	&	0
area_maxsize	&	0

	END
