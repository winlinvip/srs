	.file	"win-exception.cpp"
	.intel_syntax noprefix
	.text
	.globl	_Z16handle_exceptionv
	.def	_Z16handle_exceptionv;	.scl	2;	.type	32;	.endef
	.seh_proc	_Z16handle_exceptionv
_Z16handle_exceptionv:
.LFB95:
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
.LLSDA95:
	.byte	0xff
	.byte	0x9b
	.uleb128 .LLSDATT95-.LLSDATTD95
.LLSDATTD95:
	.byte	0x1
	.uleb128 .LLSDACSE95-.LLSDACSB95
.LLSDACSB95:
	.uleb128 .LEHB0-.LFB95
	.uleb128 .LEHE0-.LEHB0
	.uleb128 .L4-.LFB95
	.uleb128 0x1
	.uleb128 .LEHB1-.LFB95
	.uleb128 .LEHE1-.LEHB1
	.uleb128 0
	.uleb128 0
.LLSDACSE95:
	.byte	0x1
	.byte	0
	.align 4
	.long	0

.LLSDATT95:
	.text
	.seh_endproc
	.section .rdata,"dr"
.LC0:
	.ascii "r0=%d\12\0"
	.text
	.globl	_Z3fooPv
	.def	_Z3fooPv;	.scl	2;	.type	32;	.endef
	.seh_proc	_Z3fooPv
_Z3fooPv:
.LFB96:
	push	rbp
	.seh_pushreg	rbp
	mov	rbp, rsp
	.seh_setframe	rbp, 0
	sub	rsp, 48
	.seh_stackalloc	48
	.seh_endprologue
	mov	QWORD PTR 16[rbp], rcx
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
	.globl	_Z3pfnPv
	.def	_Z3pfnPv;	.scl	2;	.type	32;	.endef
	.seh_proc	_Z3pfnPv
_Z3pfnPv:
.LFB97:
	push	rbp
	.seh_pushreg	rbp
	mov	rbp, rsp
	.seh_setframe	rbp, 0
	sub	rsp, 32
	.seh_stackalloc	32
	.seh_endprologue
	mov	QWORD PTR 16[rbp], rcx
	mov	r9d, 0
	mov	r8d, 0
	mov	edx, 0
	lea	rax, _Z3fooPv[rip]
	mov	rcx, rax
	call	st_thread_create
	mov	eax, 0
	add	rsp, 32
	pop	rbp
	ret
	.seh_endproc
	.def	__main;	.scl	2;	.type	32;	.endef
	.globl	main
	.def	main;	.scl	2;	.type	32;	.endef
	.seh_proc	main
main:
.LFB98:
	push	rbp
	.seh_pushreg	rbp
	mov	rbp, rsp
	.seh_setframe	rbp, 0
	sub	rsp, 32
	.seh_stackalloc	32
	.seh_endprologue
	mov	DWORD PTR 16[rbp], ecx
	mov	QWORD PTR 24[rbp], rdx
	call	__main
	call	st_init
	cmp	DWORD PTR 16[rbp], 2
	jle	.L10
	mov	ecx, 0
	call	_Z3fooPv
	jmp	.L11
.L10:
	cmp	DWORD PTR 16[rbp], 1
	jle	.L12
	mov	r9d, 10485760
	mov	r8d, 0
	mov	edx, 0
	lea	rax, _Z3pfnPv[rip]
	mov	rcx, rax
	call	st_thread_create
	jmp	.L11
.L12:
	mov	r9d, 10485760
	mov	r8d, 0
	mov	edx, 0
	lea	rax, _Z3fooPv[rip]
	mov	rcx, rax
	call	st_thread_create
.L11:
	mov	ecx, 0
	call	st_thread_exit
	mov	eax, 0
	add	rsp, 32
	pop	rbp
	ret
	.seh_endproc
	.ident	"GCC: (GNU) 11.4.0"
	.def	__cxa_allocate_exception;	.scl	2;	.type	32;	.endef
	.def	__cxa_throw;	.scl	2;	.type	32;	.endef
	.def	__cxa_begin_catch;	.scl	2;	.type	32;	.endef
	.def	__cxa_end_catch;	.scl	2;	.type	32;	.endef
	.def	printf;	.scl	2;	.type	32;	.endef
	.def	st_thread_create;	.scl	2;	.type	32;	.endef
	.def	st_init;	.scl	2;	.type	32;	.endef
	.def	st_thread_exit;	.scl	2;	.type	32;	.endef
	.section	.rdata$.refptr._ZTIi, "dr"
	.globl	.refptr._ZTIi
	.linkonce	discard
.refptr._ZTIi:
	.quad	_ZTIi
