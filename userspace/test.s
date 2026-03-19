; ============================================================================
; Pure Flat Binary Test Program (No Header!)
;
; Simple test that:
; 1. Sets EAX to 0xDEADBEEF
; 2. Loops forever
; ============================================================================

[BITS 32]
[ORG 0x08000000]

; Code starts immediately at offset 0
start:
    ; Set EAX to recognizable value
    mov eax, 0xDEADBEEF
    mov eax, 1
    mov ebx, name
    mov ecx, 2
    int 0x80

    mov ebx, eax
    mov eax, 4
    mov ecx, str
    mov edx, 3
    int 0x80
    
    ; Infinite loop
.loop:
    jmp .loop


name: db "/dev/tty0", 0
str: db "AAA", 0