        GET     s.ARM32

; char *strdup(const char *) -- return a newly allocated copy of a string

        AREA    |Asm$$Code|, CODE, READONLY

        EXPORT  strdup

        IMPORT  malloc
        IMPORT  strcpy
        IMPORT  strlen

        =       "strdup",0,0
        &       &FF000008
strdup  MOV     ip,sp
        STMDB   sp!,{v1,fp,ip,lr,pc}
        SUB     fp,ip,#4
        MOV     v1,a1
        BL      strlen
        ADD     a1,a1,#1
        BL      malloc
        MOV     a2,v1
        MOVS    v1,a1
        BLNE    strcpy
        MOV     a1,v1
        LDMDB   fp,{v1,fp,sp,pc}$_HAT

        END
