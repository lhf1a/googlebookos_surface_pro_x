.text
.global run_cache_code
.type run_cache_code,%function
run_cache_code:
    stp x29,x30,[sp,#-48]!
    stp x21,x23,[sp,#16]
    str x24,[sp,#32]
    mov x16,x0
    mov w23,w1
    blr x16
    ldr x24,[sp,#32]
    ldp x21,x23,[sp,#16]
    ldp x29,x30,[sp],#48
    ret
.size run_cache_code,.-run_cache_code
.section .note.GNU-stack,"",%progbits
