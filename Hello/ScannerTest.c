#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#define DEVICE_PATH "/dev/Scanner"

typedef struct {
  ssize_t rc;
  const char *bytes;
} ReadStep;

typedef struct {
  const char *name;
  const char *separators;
  size_t separators_len;
  const char *data;
  size_t data_len;
  size_t read_size;
  const ReadStep *steps;
  size_t step_count;
} TestCase;

static int tests_run=0;
static int tests_failed=0;

static void fail(const char *fmt, ...) {
  va_list ap;

  va_start(ap,fmt);
  vfprintf(stderr,fmt,ap);
  va_end(ap);
  fputc('\n',stderr);
}

static int open_scanner(void) {
  int fd=open(DEVICE_PATH,O_RDWR);
  if (fd<0)
    fail("open(%s) failed: %s",DEVICE_PATH,strerror(errno));
  return fd;
}

static int write_exact(int fd,const char *buf,size_t len,const char *what) {
  ssize_t rc=write(fd,buf,len);
  if (rc!=(ssize_t)len) {
    fail("%s write expected %zu byte(s), got %zd (errno=%d: %s)",
        what,
        len,
        rc,
        errno,
        strerror(errno));
    return 0;
  }
  return 1;
}

static int set_separators(int fd,const char *separators,size_t len) {
  if (ioctl(fd,0,0)!=0) {
    fail("ioctl(fd, 0, 0) failed: errno=%d: %s",errno,strerror(errno));
    return 0;
  }
  return write_exact(fd,separators,len,"separator");
}

static int set_data(int fd,const char *data,size_t len) {
  return write_exact(fd,data,len,"data");
}

static int expect_step(int fd,
    const char *test_name,
    size_t step_index,
    size_t read_size,
    const ReadStep *expected) {
  char buf[256];
  ssize_t rc;
  int saved_errno;

  if (read_size>=sizeof(buf)) {
    fail("%s step %zu uses read_size=%zu, but max supported is %zu",
        test_name,
        step_index+1,
        read_size,
        sizeof(buf)-1);
    return 0;
  }

  errno=0;
  rc=read(fd,buf,read_size);
  saved_errno=errno;

  if (rc!=expected->rc) {
    fail("%s step %zu expected rc=%zd, got %zd (errno=%d: %s)",
        test_name,
        step_index+1,
        expected->rc,
        rc,
        saved_errno,
        strerror(saved_errno));
    return 0;
  }

  if (rc>0) {
    if (!expected->bytes) {
      fail("%s step %zu expected byte payload missing",test_name,step_index+1);
      return 0;
    }
    if (memcmp(buf,expected->bytes,(size_t)rc)!=0) {
      buf[rc]=0;
      fail("%s step %zu expected bytes \"%s\", got \"%s\"",
          test_name,
          step_index+1,
          expected->bytes,
          buf);
      return 0;
    }
  }

  return 1;
}

static int run_case(const TestCase *tc) {
  int fd;
  size_t i;

  fd=open_scanner();
  if (fd<0)
    return 0;

  if (!set_separators(fd,tc->separators,tc->separators_len)) {
    close(fd);
    return 0;
  }
  if (!set_data(fd,tc->data,tc->data_len)) {
    close(fd);
    return 0;
  }

  for (i=0; i<tc->step_count; i++) {
    if (!expect_step(fd,tc->name,i,tc->read_size,&tc->steps[i])) {
      close(fd);
      return 0;
    }
  }

  close(fd);
  return 1;
}

static int test_multiple_writes_reset_data(void) {
  static const ReadStep second_read_steps[]={
    {5,"reset"},
    {0,NULL},
    {-1,NULL}
  };
  int fd=open_scanner();
  size_t i;

  if (fd<0)
    return 0;

  if (!set_separators(fd,":",1) ||
      !set_data(fd,"a:b",3) ||
      !expect_step(fd,"multiple writes reset data",0,8,&(ReadStep){1,"a"}) ||
      !set_data(fd,"reset",5)) {
    close(fd);
    return 0;
  }

  for (i=0; i<sizeof(second_read_steps)/sizeof(second_read_steps[0]); i++) {
    if (!expect_step(fd,
            "multiple writes reset data",
            i+1,
            8,
            &second_read_steps[i])) {
      close(fd);
      return 0;
    }
  }

  close(fd);
  return 1;
}

static int test_separators_replace_only_once(void) {
  static const ReadStep steps[]={
    {1,","},
    {0,NULL},
    {-1,NULL}
  };
  int fd=open_scanner();
  size_t i;

  if (fd<0)
    return 0;

  if (!set_separators(fd,":",1) ||
      !set_data(fd,"a:b",3) ||
      !write_exact(fd,",",1,"plain data")) {
    close(fd);
    return 0;
  }

  for (i=0; i<sizeof(steps)/sizeof(steps[0]); i++) {
    if (!expect_step(fd,
            "separator update affects only one write",
            i,
            8,
            &steps[i])) {
      close(fd);
      return 0;
    }
  }

  close(fd);
  return 1;
}

static int test_independent_open_state(void) {
  int left_fd=-1;
  int right_fd=-1;

  left_fd=open_scanner();
  right_fd=open_scanner();
  if (left_fd<0 || right_fd<0)
    goto fail_out;

  if (!set_separators(left_fd,":",1) ||
      !set_separators(right_fd,",",1) ||
      !set_data(left_fd,"a:b,c",5) ||
      !set_data(right_fd,"a:b,c",5))
    goto fail_out;

  if (!expect_step(left_fd,"independent open state",0,8,&(ReadStep){1,"a"}) ||
      !expect_step(left_fd,"independent open state",1,8,&(ReadStep){0,NULL}) ||
      !expect_step(right_fd,"independent open state",2,8,&(ReadStep){3,"a:b"}) ||
      !expect_step(right_fd,"independent open state",3,8,&(ReadStep){0,NULL}))
    goto fail_out;

  close(left_fd);
  close(right_fd);
  return 1;

fail_out:
  if (left_fd>=0)
    close(left_fd);
  if (right_fd>=0)
    close(right_fd);
  return 0;
}

int main(void) {
  static const ReadStep empty_input_steps[]={
    {-1,NULL},
    {-1,NULL}
  };
  static const ReadStep one_token_steps[]={
    {3,"abc"},
    {0,NULL},
    {-1,NULL},
    {-1,NULL}
  };
  static const ReadStep multi_token_steps[]={
    {1,"a"},
    {0,NULL},
    {1,"b"},
    {0,NULL},
    {1,"c"},
    {0,NULL},
    {-1,NULL}
  };
  static const ReadStep leading_trailing_steps[]={
    {1,"a"},
    {0,NULL},
    {1,"b"},
    {0,NULL},
    {-1,NULL}
  };
  static const ReadStep repeated_separator_steps[]={
    {1,"a"},
    {0,NULL},
    {1,"b"},
    {0,NULL},
    {1,"c"},
    {0,NULL},
    {-1,NULL}
  };
  static const ReadStep no_separator_steps[]={
    {3,"abc"},
    {0,NULL},
    {-1,NULL}
  };
  static const ReadStep empty_separator_set_steps[]={
    {5,"a:b c"},
    {0,NULL},
    {-1,NULL}
  };
  static const ReadStep chunked_steps[]={
    {2,"ab"},
    {2,"cd"},
    {2,"ef"},
    {0,NULL},
    {2,"gh"},
    {0,NULL},
    {-1,NULL}
  };
  static const ReadStep bytewise_steps[]={
    {1,"a"},
    {1,"b"},
    {0,NULL},
    {1,"c"},
    {1,"d"},
    {0,NULL},
    {-1,NULL}
  };
  static const ReadStep all_separator_steps[]={
    {-1,NULL},
    {-1,NULL}
  };
  static const ReadStep separator_replacement_steps[]={
    {1,"a"},
    {0,NULL},
    {1,"b"},
    {0,NULL},
    {1,"c"},
    {0,NULL},
    {-1,NULL}
  };

  static const TestCase tests[]={
    {"empty input returns end-of-data",":",1,"",0,8,empty_input_steps,sizeof(empty_input_steps)/sizeof(empty_input_steps[0])},
    {"single token returns data then 0 then -1",":",1,"abc",3,8,one_token_steps,sizeof(one_token_steps)/sizeof(one_token_steps[0])},
    {"multiple tokens with colon separators",":",1,"a:b:c",5,8,multi_token_steps,sizeof(multi_token_steps)/sizeof(multi_token_steps[0])},
    {"leading and trailing separators are skipped",":",1,"::a:b::",7,8,leading_trailing_steps,sizeof(leading_trailing_steps)/sizeof(leading_trailing_steps[0])},
    {"repeated separators do not create empty tokens",":",1,"a::b:::c",8,8,repeated_separator_steps,sizeof(repeated_separator_steps)/sizeof(repeated_separator_steps[0])},
    {"no separators present yields one token",":",1,"abc",3,8,no_separator_steps,sizeof(no_separator_steps)/sizeof(no_separator_steps[0])},
    {"empty separator set makes whole input one token","",0,"a:b c",5,8,empty_separator_set_steps,sizeof(empty_separator_set_steps)/sizeof(empty_separator_set_steps[0])},
    {"small reads split a token across calls",":",1,"abcdef:gh",9,2,chunked_steps,sizeof(chunked_steps)/sizeof(chunked_steps[0])},
    {"read size 1 still gives one zero between tokens",":",1,"ab:cd",5,1,bytewise_steps,sizeof(bytewise_steps)/sizeof(bytewise_steps[0])},
    {"input containing only separators has no tokens",":",1,":::",3,8,all_separator_steps,sizeof(all_separator_steps)/sizeof(all_separator_steps[0])},
    {"separator replacement can be changed later",",;",2,"a,b;c",5,8,separator_replacement_steps,sizeof(separator_replacement_steps)/sizeof(separator_replacement_steps[0])}
  };
  size_t i;

  for (i=0; i<sizeof(tests)/sizeof(tests[0]); i++) {
    int ok;

    tests_run++;
    ok=run_case(&tests[i]);
    printf("[%s] %s\n",ok ? "PASS" : "FAIL",tests[i].name);
    if (!ok)
      tests_failed++;
  }

  tests_run++;
  if (test_multiple_writes_reset_data())
    printf("[PASS] multiple writes reset data and scan position\n");
  else {
    printf("[FAIL] multiple writes reset data and scan position\n");
    tests_failed++;
  }

  tests_run++;
  if (test_separators_replace_only_once())
    printf("[PASS] separator update affects only one write\n");
  else {
    printf("[FAIL] separator update affects only one write\n");
    tests_failed++;
  }

  tests_run++;
  if (test_independent_open_state())
    printf("[PASS] each open file descriptor has independent state\n");
  else {
    printf("[FAIL] each open file descriptor has independent state\n");
    tests_failed++;
  }

  printf("\nSummary: %d/%d tests passed\n",
      tests_run-tests_failed,
      tests_run);
  return (tests_failed==0 ? 0 : 1);
}
