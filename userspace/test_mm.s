; ============================================================================
; Memory Management Test Program
; Tests: brk (heap), mmap (anonymous), stack growth
; ============================================================================

[BITS 32]
[ORG 0x08000000]

; System call numbers
SYS_READ   equ 3
SYS_WRITE  equ 4
SYS_OPEN   equ 5
SYS_CLOSE  equ 6
SYS_BRK    equ 45
SYS_MMAP   equ 90
SYS_MUNMAP equ 91

; mmap flags
PROT_READ  equ 0x1
PROT_WRITE equ 0x2
PROT_EXEC  equ 0x4
MAP_PRIVATE   equ 0x02
MAP_ANONYMOUS equ 0x20

start:
    ; Open VGA device for output
    mov eax, SYS_OPEN
    mov ebx, dev_vga
    mov ecx, 0
    int 0x80
    mov [vga_fd], eax
    
    ; Print banner
    call print_banner
    
    ; Test 1: Query current brk
    call test_brk_query
    
    ; Test 2: Allocate heap (brk)
    call test_brk_alloc
    
    ; Test 3: Write to heap (triggers page fault)
    call test_heap_write
    
    ; Test 4: Anonymous mmap
    call test_mmap
    
    ; Test 5: Deep stack (triggers stack growth)
    call test_stack_growth
    
    ; Print success
    mov eax, SYS_WRITE
    mov ebx, [vga_fd]
    mov ecx, msg_success
    mov edx, msg_success_len
    int 0x80
    
    ; Exit loop
.loop:
    hlt
    jmp .loop

; ============================================================================
; Test Functions
; ============================================================================

print_banner:
    mov eax, SYS_WRITE
    mov ebx, [vga_fd]
    mov ecx, banner
    mov edx, banner_len
    int 0x80
    ret

test_brk_query:
    ; Query current brk
    mov eax, SYS_BRK
    mov ebx, 0
    int 0x80
    mov [initial_brk], eax
    
    ; Print message
    mov eax, SYS_WRITE
    mov ebx, [vga_fd]
    mov ecx, msg_brk_query
    mov edx, msg_brk_query_len
    int 0x80
    ret

test_brk_alloc:
    ; Allocate 8KB heap
    mov eax, [initial_brk]
    add eax, 8192
    mov [new_brk], eax
    
    mov eax, SYS_BRK
    mov ebx, [new_brk]
    int 0x80
    
    ; Print message
    mov eax, SYS_WRITE
    mov ebx, [vga_fd]
    mov ecx, msg_brk_alloc
    mov edx, msg_brk_alloc_len
    int 0x80
    ret

test_heap_write:
    ; Write pattern to heap (triggers page fault for lazy allocation)
    mov edi, [initial_brk]
    mov ecx, 2048          ; Write 2048 dwords (8KB)
    mov eax, 0xCAFEBABE
.loop:
    mov [edi], eax
    add edi, 4
    loop .loop
    
    ; Verify first write
    mov edi, [initial_brk]
    mov eax, [edi]
    cmp eax, 0xCAFEBABE
    jne .error
    
    ; Print success
    mov eax, SYS_WRITE
    mov ebx, [vga_fd]
    mov ecx, msg_heap_write
    mov edx, msg_heap_write_len
    int 0x80
    ret

.error:
    mov eax, SYS_WRITE
    mov ebx, [vga_fd]
    mov ecx, msg_error
    mov edx, msg_error_len
    int 0x80
    ret

test_mmap:
    ; mmap 16KB anonymous memory
    mov eax, SYS_MMAP
    mov ebx, 0              ; addr hint (0 = kernel chooses)
    mov ecx, 16384          ; length (16KB)
    mov edx, PROT_READ | PROT_WRITE
    mov esi, MAP_PRIVATE | MAP_ANONYMOUS
    mov edi, -1             ; fd (-1 for anonymous)
    int 0x80
    
    cmp eax, -1
    je .error
    mov [mmap_addr], eax
    
    ; Write to mmap region (triggers page fault)
    mov edi, eax
    mov ecx, 4096           ; Write 4096 dwords (16KB)
    mov eax, 0xDEADC0DE
.loop:
    mov [edi], eax
    add edi, 4
    loop .loop
    
    ; Verify
    mov edi, [mmap_addr]
    mov eax, [edi]
    cmp eax, 0xDEADC0DE
    jne .error
    
    ; Print success
    mov eax, SYS_WRITE
    mov ebx, [vga_fd]
    mov ecx, msg_mmap
    mov edx, msg_mmap_len
    int 0x80
    ret

.error:
    mov eax, SYS_WRITE
    mov ebx, [vga_fd]
    mov ecx, msg_error
    mov edx, msg_error_len
    int 0x80
    ret

test_stack_growth:
    ; Trigger deep recursion to test stack growth
    mov ecx, 100            ; 100 recursive calls
    call recursive_test
    
    ; Print success
    mov eax, SYS_WRITE
    mov ebx, [vga_fd]
    mov ecx, msg_stack
    mov edx, msg_stack_len
    int 0x80
    ret

recursive_test:
    ; Allocate 1KB on stack per call
    sub esp, 1024
    
    ; Write pattern to stack
    mov edi, esp
    mov eax, 0xFEEDFACE
    mov [edi], eax
    
    ; Recurse if count > 0
    dec ecx
    jz .done
    call recursive_test
.done:
    add esp, 1024
    l:
    jmp l

; ============================================================================
; Data Section
; ============================================================================

align 4
vga_fd: dd 0
initial_brk: dd 0
new_brk: dd 0
mmap_addr: dd 0

dev_vga: db "/dev/vga0", 0

banner: db "=== Memory Management Test ===", 10, 0
banner_len equ $ - banner

msg_brk_query: db "[1] brk query: OK", 10, 0
msg_brk_query_len equ $ - msg_brk_query

msg_brk_alloc: db "[2] brk alloc 8KB: OK", 10, 0
msg_brk_alloc_len equ $ - msg_brk_alloc

msg_heap_write: db "[3] heap write (lazy alloc): OK", 10, 0
msg_heap_write_len equ $ - msg_heap_write

msg_mmap: db "[4] mmap 16KB (lazy alloc): OK", 10, 0
msg_mmap_len equ $ - msg_mmap

msg_stack: db "[5] stack growth (100KB): OK", 10, 0
msg_stack_len equ $ - msg_stack

msg_success: db 10, "All tests passed!", 10, 0
msg_success_len equ $ - msg_success

msg_error: db "ERROR!", 10, 0
msg_error_len equ $ - msg_error