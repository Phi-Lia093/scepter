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
    
    ; Infinite loop
.loop:
    jmp .loop