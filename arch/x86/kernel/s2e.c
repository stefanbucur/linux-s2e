#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <asm/s2e/s2e.h>

static int s2e_active = 0;

enum
{
    S2E_THREAD_START = 0xBEEF,
    S2E_THREAD_EXIT,
    S2E_VM_ALLOC
};

struct s2e_thread_struct
{
    int              pid;
    const char       *name;
    unsigned long    start, end;
    unsigned long    stack_top;
    unsigned long    address_space;
} __attribute__((packed));

struct s2e_vmmap_struct
{
    int pid;
    unsigned long  start, end;
    const char     *name;
    int            writable;
    int            executable;
} __attribute__((packed));

static inline int s2e_version(void)
{
    int version = 0;
    __asm__ __volatile__(
        S2E_INSTRUCTION_SIMPLE(00)
        : "=a" (version)  : "a" (0)
    );
    return version;
}

static int s2e_system_call(unsigned int id, void *data, unsigned int size) {
    int result = -1;

    __asm__ __volatile__ (
            S2E_INSTRUCTION_SIMPLE(B0)
            : "=a" (result) : "a" (id), "c" (data), "d" (size) : "memory"
    );

    return result;
}

static inline void s2e_message(const char *message)
{
    __asm__ __volatile__(
        S2E_INSTRUCTION_SIMPLE(10)
        : : "a" (message)
        );
}

static inline int s2e_printf(const char *format, ...)
{
    char buffer[512];
    int ret;
    va_list args;
    va_start(args, format);
    ret = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    s2e_message(buffer);
    return ret;
}

static void s2e_notify_address_space(struct task_struct *task) {
    struct vm_area_struct *vm;

    if (!task->mm) {
        return;
    }

    vm = task->mm->mmap;
    while (vm)
    {
        struct s2e_vmmap_struct s2e_vmmap = { 0 };
        s2e_vmmap.pid = task->pid;

        if (vm->vm_file)
            s2e_vmmap.name = vm->vm_file->f_path.dentry->d_name.name;
        else if (!(s2e_vmmap.name = arch_vma_name(vm)))
            s2e_vmmap.name = "[unknown]";

        s2e_vmmap.start = vm->vm_start;
        s2e_vmmap.end = vm->vm_end;
        s2e_vmmap.writable = !!(vm->vm_flags & VM_WRITE);
        s2e_vmmap.executable = !!(vm->vm_flags & VM_EXEC);

        s2e_system_call(S2E_VM_ALLOC, &s2e_vmmap, sizeof(s2e_vmmap));

        vm = vm->vm_next;
    }
}

static void s2e_notify_thread(struct task_struct *task) {
    struct s2e_thread_struct s2e_thread = { 0 };
    char name[TASK_COMM_LEN];

    get_task_comm(name, task);
    s2e_thread.pid = task->pid;
    s2e_thread.name = name;
    if (task->mm) {
        s2e_thread.start = task->mm->start_code;
        s2e_thread.end = task->mm->end_code;
        s2e_thread.stack_top = task->mm->start_stack;

        /* The physical address of the page table is stored in the CR3 register
         * when the task is active. */
        s2e_thread.address_space = (unsigned long)__pa(task->mm->pgd);
    }

#if 0
    s2e_printf("S2E: thread for %s[%d] (%d) [0x%lx-0x%lx] -- stack -- %p to %p\n", name,
               task->pid, task->tgid,
               s2e_thread.start, s2e_thread.end,
               task_thread_info(task), end_of_stack(task));
#endif

    s2e_system_call(S2E_THREAD_START, &s2e_thread, sizeof(s2e_thread));

    s2e_notify_address_space(task);
}

static void s2e_notify_initial_state(struct task_struct *task) {
    struct task_struct *other_task;

    if (s2e_active == s2e_version())
        return;

    s2e_active = s2e_version();

    for_each_process(other_task)
    {
        if (other_task == task)
            continue;
        s2e_notify_start_thread(other_task);
    }
}

void s2e_notify_start_thread(struct task_struct *task)
{
    s2e_notify_initial_state(task);
    s2e_notify_thread(task);
}

void s2e_notify_exit_thread(struct task_struct *task) {
    int pid = (int)task->pid;
    char name[TASK_COMM_LEN];
    get_task_comm(name, task);

    s2e_notify_initial_state(NULL);

#if 0
    s2e_printf("S2E: exit thread %s[%d]\n", name, task->pid);
#endif

    s2e_system_call(S2E_THREAD_EXIT, &pid, sizeof(pid));
}


