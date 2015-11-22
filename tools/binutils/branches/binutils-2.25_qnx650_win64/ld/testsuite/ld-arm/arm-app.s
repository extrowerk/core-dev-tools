	.text
	.globl _start
	.type   _start, %function
_start:
	mov	ip, sp
	stmdb	sp!, {r11, ip, lr, pc}
	bl	app_func
	ldmia	sp, {r11, sp, lr}
	bx lr

	.globl app_func
	.type  app_func, %function
app_func:
	mov	ip, sp
	stmdb	sp!, {r11, ip, lr, pc}
	bl	lib_func1
	ldmia	sp, {r11, sp, lr}
	bx lr

	.globl app_func2
	.type  app_func2, %function
app_func2:
	bx	lr

	.data
	.long data_obj
