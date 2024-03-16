;
; nasm -f win64 -o win-nasm-hello.o win-nasm-hello.asm && g++ -o win-nasm-hello win-nasm-hello.o && ./win-nasm-hello
; See https://sonictk.github.io/asm_tutorial/#introduction/settingup/hello,world
;

bits 64
default rel

segment .data
    msg db "Hello World, Cygwin ASM!", 0xd, 0xa, 0

segment .text
global main
extern printf
extern ExitProcess

main:
    push rbp
    mov rbp, rsp
    sub rsp, 0x20

    lea     rcx, [msg]
    call    printf

    xor rax, rax
    call ExitProcess
