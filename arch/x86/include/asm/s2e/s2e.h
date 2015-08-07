#ifndef _S2E_H_
#define _S2E_H_

/* TODO: Move this in an architecture-specific place. */

#ifdef S2E_DISPATCH_CUSTOM
/* (Deprecated) We define our own custom instruction, using a previously
 * unallocated opcode sequence. Therefore, this construct would cause a
 * processor exception on real hardware or non-S2E virtual environments.
 */
#define S2E_INSTRUCTION_COMPLEX(val1, val2)             \
    ".byte 0x0F, 0x3F\n"                                \
    ".byte 0x00, 0x" #val1 ", 0x" #val2 ", 0x00\n"      \
    ".byte 0x00, 0x00, 0x00, 0x00\n"

#else
/* We overload a multi-byte NOP instruction.  We reuse the 8-byte form
 * NOP DWORD ptr [EAX + EAX*1 + 00000000H], corresponding to the sequence
 * 0F 1F 84 00 00 00 00 00H
 *
 * The last five bytes can be changed arbitrarily, and we use them as follows:
 *   Byte 3 - The S2E magic number 0x42
 *   Byte 4-7 - A 32-bit S2E instruction payload.
 */
#define S2E_INSTRUCTION_COMPLEX(val1, val2)             \
    ".byte 0x0F, 0x1F\n"                                \
    ".byte 0x84, 0x42\n"                                \
    ".byte 0x00, 0x" #val1 ", 0x" #val2 ", 0x00\n"
#endif /* defined(S2E_DISPATCH_CUSTOM) */

#define S2E_INSTRUCTION_SIMPLE(val)                     \
    S2E_INSTRUCTION_COMPLEX(val, 00)


#define S2E_INSTRUCTION_REGISTERS_COMPLEX(val1, val2)   \
        "pushl %%ebx\n"                                 \
        "movl %%edx, %%ebx\n"                           \
        S2E_INSTRUCTION_COMPLEX(val1, val2)             \
        "popl %%ebx\n"

#define S2E_INSTRUCTION_REGISTERS_SIMPLE(val)           \
        S2E_INSTRUCTION_REGISTERS_COMPLEX(val, 00)


#define S2E_CONCRETE_PROLOGUE \
        "push %%ebx\n"        \
        "push %%esi\n"        \
        "push %%edi\n"        \
        "push %%ebp\n"        \
        "xor %%ebx, %%ebx\n"  \
        "xor %%ebp, %%ebp\n"  \
        "xor %%esi, %%esi\n"  \
        "xor %%edi, %%edi\n"

#define S2E_CONCRETE_EPILOGUE \
        "pop %%ebp\n"         \
        "pop %%edi\n"         \
        "pop %%esi\n"         \
        "pop %%ebx\n"


struct task_struct;
void s2e_notify_start_thread(struct task_struct *task);
void s2e_notify_exit_thread(struct task_struct *task);
void s2e_notify_user_leave(struct task_struct *task);
void s2e_notify_user_enter(struct task_struct *task);


#endif /* _S2E_H_ */
