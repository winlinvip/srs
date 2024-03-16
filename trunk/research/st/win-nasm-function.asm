;
; nasm -f win64 -o win-nasm-function.o win-nasm-function.asm && g++ -o win-nasm-function win-nasm-function.o && ./win-nasm-function
; See https://sonictk.github.io/asm_tutorial/#usingassemblyinc/c++programs/afactorialfunction
;

bits 64
default rel

segment .text
global main
global foo
extern ExitProcess

foo:
    push rbp
    mov rbp, rsp
    sub rsp, 0x20

    ; Or
    ;add rsp, 0x20
    ;pop rbp
    ;ret

    ; Or
    ;mov rsp, rbp
    ;pop rbp
    ;ret

    ; Or
    leave
    ret

main:
    push rbp
    mov rbp, rsp
    sub rsp, 0x20

    call foo

    xor rax, rax
    call ExitProcess
