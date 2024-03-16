	.file	"win-exception-min.cpp"
	.intel_syntax noprefix
	.text
	.globl	_Z16handle_exceptionv
	.def	_Z16handle_exceptionv;	.scl	2;	.type	32;	.endef
	.seh_proc	_Z16handle_exceptionv
_Z16handle_exceptionv:
.LFB2:
	push	rbp
	.seh_pushreg	rbp
	push	rbx
	.seh_pushreg	rbx
	sub	rsp, 40
	.seh_stackalloc	40
	lea	rbp, 32[rsp]
	.seh_setframe	rbp, 32
	.seh_endprologue
	mov	ecx, 4
	call	__cxa_allocate_exception
	mov	DWORD PTR [rax], 3
	mov	r8d, 0
	mov	rdx, QWORD PTR .refptr._ZTIi[rip]
	mov	rcx, rax
.LEHB0:
	call	__cxa_throw
.LEHE0:
.L4:
	mov	rcx, rax
	call	__cxa_begin_catch
	mov	ebx, 5
.LEHB1:
	call	__cxa_end_catch
.LEHE1:
	mov	eax, ebx
	add	rsp, 40
	pop	rbx
	pop	rbp
	ret
	.def	__gxx_personality_seh0;	.scl	2;	.type	32;	.endef
	.seh_handler	__gxx_personality_seh0, @unwind, @except
	.seh_handlerdata
	.align 4
.LLSDA2:
	.byte	0xff
	.byte	0x9b
	.uleb128 .LLSDATT2-.LLSDATTD2
.LLSDATTD2:
	.byte	0x1
	.uleb128 .LLSDACSE2-.LLSDACSB2
.LLSDACSB2:
	.uleb128 .LEHB0-.LFB2
	.uleb128 .LEHE0-.LEHB0
	.uleb128 .L4-.LFB2
	.uleb128 0x1
	.uleb128 .LEHB1-.LFB2
	.uleb128 .LEHE1-.LEHB1
	.uleb128 0
	.uleb128 0
.LLSDACSE2:
	.byte	0x1
	.byte	0
	.align 4
	.long	0

.LLSDATT2:
	.text
	.seh_endproc
	.def	__main;	.scl	2;	.type	32;	.endef
	.section .rdata,"dr"
.LC0:
	.ascii "r0=%d\12\0"
	.text
	.globl	main
	.def	main;	.scl	2;	.type	32;	.endef
	.seh_proc	main
main:
.LFB3:
	push	rbp
	.seh_pushreg	rbp
	mov	rbp, rsp
	.seh_setframe	rbp, 0
	sub	rsp, 48
	.seh_stackalloc	48
	.seh_endprologue
	mov	DWORD PTR 16[rbp], ecx
	mov	QWORD PTR 24[rbp], rdx
	call	__main
	call	_Z16handle_exceptionv
	mov	DWORD PTR -4[rbp], eax
	mov	eax, DWORD PTR -4[rbp]
	mov	edx, eax
	lea	rax, .LC0[rip]
	mov	rcx, rax
	call	printf
	mov	eax, 0
	add	rsp, 48
	pop	rbp
	ret
	.seh_endproc
	.ident	"GCC: (GNU) 11.4.0"
	.def	__cxa_allocate_exception;	.scl	2;	.type	32;	.endef
	.def	__cxa_throw;	.scl	2;	.type	32;	.endef
	.def	__cxa_begin_catch;	.scl	2;	.type	32;	.endef
	.def	__cxa_end_catch;	.scl	2;	.type	32;	.endef
	.def	printf;	.scl	2;	.type	32;	.endef
	.section	.rdata$.refptr._ZTIi, "dr"
	.globl	.refptr._ZTIi
	.linkonce	discard
.refptr._ZTIi:
	.quad	_ZTIi
