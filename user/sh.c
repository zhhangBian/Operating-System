#include <args.h>
#include <lib.h>

#define WHITESPACE " \t\r\n"
#define SYMBOLS "<|>&;()"

/* Overview:
 *   Parse the next token from the string at s.
 *
 * Post-Condition:
 *   Set '*p1' to the beginning of the token and '*p2' to just past the token.
 *   Return:
 *     - 0 if the end of string is reached.
 *     - '<' for < (stdin redirection).
 *     - '>' for > (stdout redirection).
 *     - '|' for | (pipe).
 *     - 'w' for a word (command, argument, or file name).
 *
 *   The buffer is modified to turn the spaces after words into zero bytes ('\0'), so that the
 *   returned token is a null-terminated string.
 */
int _gettoken(char *s, char **p1, char **p2) {
  *p1 = 0;
  *p2 = 0;
  if (s == 0) {
    return 0;
  }

  while (strchr(WHITESPACE, *s)) {
    *s++ = 0;
  }
  if (*s == 0) {
    return 0;
  }

  if (strchr(SYMBOLS, *s)) {
    int t = *s;
    *p1 = s;
    *s++ = 0;
    *p2 = s;
    return t;
  }

  *p1 = s;
  while (*s && !strchr(WHITESPACE SYMBOLS, *s)) {
    s++;
  }
  *p2 = s;
  return 'w';
}

int gettoken(char *s, char **p1) {
  static int c, nc;
  static char *np1, *np2;

  if (s) {
    nc = _gettoken(s, &np1, &np2);
    return 0;
  }
  c = nc;
  *p1 = np1;
  nc = _gettoken(np2, &np1, &np2);
  return c;
}

#define MAXARGS 128

int parsecmd(char **argv, int *rightpipe) {
  int argc = 0;
  while (1) {
    char *t;
    int fd, r;
    int c = gettoken(0, &t);
    switch (c) {
    case 0:
      return argc;
    case 'w':
      if (argc >= MAXARGS) {
        debugf("too many arguments\n");
        exit();
      }
      argv[argc++] = t;
      break;
    case '<':
      if (gettoken(0, &t) != 'w') {
        debugf("syntax error: < not followed by word\n");
        exit();
      }
      // Open 't' for reading, dup it onto fd 0, and then close the original fd.
      // If the 'open' function encounters an error,
      // utilize 'debugf' to print relevant messages,
      // and subsequently terminate the process using 'exit'.
      /* Exercise 6.5: Your code here. (1/3) */
      fd = open(t, O_RDONLY);
      if(fd < 0) {
        debugf("open error\n");
        exit();
      }
      dup(fd, 0);
      close(fd);
      break;
    case '>':
      if (gettoken(0, &t) != 'w') {
        debugf("syntax error: > not followed by word\n");
        exit();
      }
      // Open 't' for writing, create it if not exist and trunc it if exist, dup
      // it onto fd 1, and then close the original fd.
      // If the 'open' function encounters an error,
      // utilize 'debugf' to print relevant messages,
      // and subsequently terminate the process using 'exit'.
      fd = open(t, O_WRONLY);
      if(fd < 0) {
        debugf("open error\n");
        exit();
      }
      dup(fd, 1);
      close(fd);
      break;
    case '|':;
      /*
       * First, allocate a pipe.
       * Then fork, set '*rightpipe' to the returned child envid or zero.
       * The child runs the right side of the pipe:
       * - dup the read end of the pipe onto 0
       * - close the read end of the pipe
       * - close the write end of the pipe
       * - and 'return parsecmd(argv, rightpipe)' again, to parse the rest of the
       *   command line.
       * The parent runs the left side of the pipe:
       * - dup the write end of the pipe onto 1
       * - close the write end of the pipe
       * - close the read end of the pipe
       * - and 'return argc', to execute the left of the pipeline.
       */
      int p[2];
      /* Exercise 6.5: Your code here. (3/3) */
      pipe(p);
      *rightpipe = fork();
      if (*rightpipe == 0) {
        dup(p[0], 0);
        close(p[0]);
        close(p[1]);
        return parsecmd(argv, rightpipe);
      }  else if (*rightpipe > 0) {
        dup(p[1], 1);
        close(p[1]);
        close(p[0]);
        return argc;
      }
      break;
    }
  }

  return argc;
}

// 运行指令
void runcmd(char *s) {
  gettoken(s, 0);

  char *argv[MAXARGS];
  int rightpipe = 0;
  int argc = parsecmd(argv, &rightpipe);
  if (argc == 0) {
    return;
  }
  argv[argc] = 0;

  int child = spawn(argv[0], argv);
  close_all();
  if (child >= 0) {
    wait(child);
  } else {
    debugf("spawn %s: %d\n", argv[0], child);
  }
  if (rightpipe) {
    wait(rightpipe);
  }
  exit();
}

// 从标准控制台读入一行命令，保存到buffer中
// n实际上取了buffer的大小
void readline(char *buffer, u_int n) {
  int func_info;
  for (int i = 0; i < n; i++) {
    // 挨个字节读取
    if ((func_info = read(0, buffer + i, 1)) != 1) {
      if (func_info < 0) {
        debugf("read error: %d\n", func_info);
      }
      exit();
    }

    // 如果是退格
    if (buffer[i] == '\b' || buffer[i] == 0x7f) {
      if (i > 0) {
        i -= 2;
        if (buffer[i] != '\b') {
          printf("\b");
        }
      } else {
        i = -1;
      }
    }
    // 遇到换行，代表命令结束，停止解析
    if (i>=0 && (buffer[i] == '\r' || buffer[i] == '\n')) {
      buffer[i] = 0;
      return;
    }
  }

  // 遇到命令过长的形况：不解析当行
  debugf("line too long\n");
  // 吃掉剩下的字符，避免缓冲区溢出
  while ((func_info = read(0, buffer, 1)) == 1 && 
          buffer[0] != '\r' && buffer[0] != '\n');
  buffer[0] = 0;
}

char buf[1024];

void usage(void) {
  printf("usage: sh [-ix] [script-file]\n");
  exit();
}

int main(int argc, char **argv) {
  int r;
  int interactive = iscons(0);
  int echocmds = 0;
  printf("\n:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
  printf("::                                                         ::\n");
  printf("::                     MOS Shell 2024                      ::\n");
  printf("::                                                         ::\n");
  printf(":::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
  ARGBEGIN {
    case 'i':
      interactive = 1;
      break;
    case 'x':
      echocmds = 1;
      break;
    default:
      usage();
    }
  ARGEND

  if (argc > 1) {
    usage();
  }
  if (argc == 1) {
    close(0);
    if ((r = open(argv[0], O_RDONLY)) < 0) {
      user_panic("open %s: %d", argv[0], r);
    }
    user_assert(r == 0);
  }

  for (;;) {
    if (interactive) {
      printf("\n$ ");
    }
    readline(buf, sizeof buf);

    if (buf[0] == '#') {
      continue;
    }
    if (echocmds) {
      printf("# %s\n", buf);
    }
    if ((r = fork()) < 0) {
      user_panic("fork: %d", r);
    }
    if (r == 0) {
      runcmd(buf);
      exit();
    } else {
      wait(r);
    }
  }
  return 0;
}
