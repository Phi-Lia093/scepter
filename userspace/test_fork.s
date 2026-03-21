; ============================================================================
; Fork Test Program
; Parent prints "parent" infinitely, child prints "child" 10000 times then exits
; ============================================================================

[BITS 32]
[ORG 0x08000000]

; System call numbers
SYS_EXIT  equ 1
SYS_FORK  equ 2
SYS_WRITE equ 4
SYS_OPEN  equ 5

start:
    mov eax, SYS_FORK
    int 0x80
    test eax, eax
    jz child
    
parent:
    mov eax, 1234
    int 0x80
    a:
    jmp a
    
child:
    mov eax, 5678
    int 0x80
    mov eax, 1
    mov ebx, 0
    int 0x80
    b:
    jmp b
    
    ; Should never reach here
.hang:
    hlt
    jmp .hang