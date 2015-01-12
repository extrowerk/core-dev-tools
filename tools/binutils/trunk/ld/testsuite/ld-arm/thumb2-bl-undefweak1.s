@ Test that calls to undefined weak functions resolve to call through
@ the PLT in shared libraries in ARM mode.

	.arch armv6
	.syntax unified
	.text
	.type   foo, %function
foo:
	bl bar
	.weak bar
