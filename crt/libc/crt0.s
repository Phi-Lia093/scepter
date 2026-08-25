/* ============================================================================
 * C Runtime Startup (crt0.s)
 *
 * The kernel has set ESP to point at the initial stack block it built in
 * setup_initial_stack():
 *   [esp+0]            argc
 *   [esp+4]            argv[0..argc-1], NULL
 *   [esp+4+4*(argc+1)] envp[0..envc-1], NULL
 *
 * The actual strings are packed above the pointer block (higher addresses).
 * ============================================================================ */

.section .text
.global _start

_start:
    /* Zero the frame pointer (marks the outermost stack frame) */
    xorl %ebp, %ebp

    /* Parse the initial stack: ecx=argc, edx=argv, esi=envp */
    movl  (%esp), %ecx            /* ecx = argc          */
    leal  4(%esp), %edx           /* edx = argv          */
    leal  8(%esp,%ecx,4), %esi    /* esi = envp          */

    /* Record the environment for getenv()/setenv() */
    movl  %esi, environ

    /* 16-byte align the stack (ABI) */
    andl  $-16, %esp

    /* cdecl: push envp, argv, argc */
    pushl %esi
    pushl %edx
    pushl %ecx

    /* int main(int argc, char *argv[], char *envp[]) */
    call  main

    /* main() returned: exit with its return value */
    movl  %eax, %ebx              /* exit status        */
    movl  $1, %eax                /* SYS_EXIT           */
    int   $0x80

    /* Should never reach here */
.hang:
    hlt
    jmp .hang
