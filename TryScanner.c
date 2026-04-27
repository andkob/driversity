// gcc -o TryScanner TryScanner.c -g -Wall

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#define ERR(s) err(s,__FILE__,__LINE__)

static void err(char *s, char *file, int line) {
  fprintf(stderr,"%s:%d: %s\n",file,line,s);
  exit(1);
}

int main() {
  enum {max=100};
  char buf[max+1];
  char *line=NULL;
  size_t cap=0;
  ssize_t line_len;
  ssize_t len;

  int scanner=open("/dev/Scanner",O_RDWR);
  if (scanner<0)
    ERR("open() failed");

  if (ioctl(scanner,0,0))
    ERR("ioctl() for separators failed");
  if (write(scanner,":",1)!=1)
    ERR("write() of separators failed");

  printf("input> ");
  fflush(stdout);
  while ((line_len=getline(&line,&cap,stdin))!=-1) {
    if (line_len>0 && line[line_len-1]=='\n')
      line[--line_len]=0;

    len=line_len;
    if (len!=write(scanner,line,len))
      ERR("write() of data failed");
    while ((len=read(scanner,buf,max))>=0) {
      buf[len]=0;
      printf("%s%s",buf,(len ? "" : "\n"));
    }
    printf("input> ");
    fflush(stdout);
  }

  free(line);
  close(scanner);
  return 0;
}
