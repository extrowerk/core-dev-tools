	.syntax unified
	.globl _start
	.type  _start, %function
	.globl func_to_branch_to

	.arm
	.text
	.type  func_to_branch_to, %function
func_to_branch_to:
	bx lr

	.thumb
	.section .foo, "xa"
	.thumb_func
_start:
	bl func_to_branch_to

