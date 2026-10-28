.text
.global invoke_counter_loop
invoke_counter_loop:
 stp x29,x30,[sp,#-96]!
 stp x19,x20,[sp,#16]
 stp x21,x22,[sp,#32]
 stp x23,x24,[sp,#48]
 stp x25,x26,[sp,#64]
 stp x27,x28,[sp,#80]
 sub sp,sp,#0x800
 mov x22,x1
 mov x26,x2
 mov x23,x3
 mov x19,x4
 str x5,[sp,#56]
 add x29,sp,#0x700
 mov x21,xzr
 mov w28,wzr
 mov x24,xzr
 mov x25,xzr
 mov w9,#27
 str w9,[sp,#1616]
 stur wzr,[x29,#-80]
 adr x9,loop_return
 str x9,[sp,#2032]
 br x0
loop_return:
 add sp,sp,#0x800
 ldp x19,x20,[sp,#16]
 ldp x21,x22,[sp,#32]
 ldp x23,x24,[sp,#48]
 ldp x25,x26,[sp,#64]
 ldp x27,x28,[sp,#80]
 ldp x29,x30,[sp],#96
 ret
.section .note.GNU-stack,"",%progbits
