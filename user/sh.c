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
// 根据解析到的token性质返回信息
// - 0：解析到字符串末尾
// - <：输入重定向
// - >：输出重定向
// - |：管道
// - w：词：指令、文件名、其他所有情况
int _gettoken(char *str, char **token_pointer, char **next_token_pointer) {
  *token_pointer = 0;
  *next_token_pointer = 0;
  // 如果是null指针
  if (str == 0) {
    return 0;
  }

  // 跳过无用字符
  while (strchr(WHITESPACE, *str)) {
    *str++ = 0;
  }
  // 如果来到字符串末尾
  if (*str == 0) {
    return 0;
  }

  // 解析到关键字符 "<|>&;()"
  if (strchr(SYMBOLS, *str)) {
    int tmp = *str;
    *token_pointer = str;
    *str = 0;
    *next_token_pointer = ++str;
    return tmp;
  }
  // 解析到有意义字符
  *token_pointer = str;
  while (*str && !strchr(WHITESPACE SYMBOLS, *str)) {
    str++;
  }

  *next_token_pointer = str;
  return 'w';
}

int gettoken(char *s, char **token_pointer) {
  // 设为静态变量，保证操作的连续
  static char *token, *next_token;
  static int c, nc;

  if (s) {
    nc = _gettoken(s, &token, &next_token);
    return 0;
  }
  // 对于获取上一个token的情况，传入s为0即可
  c = nc;
  *token_pointer = token;
  nc = _gettoken(next_token, &token, &next_token);
  return c;
}

#define MAXARGS 128

int parsecmd(char **argv, int *rightpipe) {
  int argc = 0;
  while (1) {
    char *token;
    int fd;
    int c = gettoken(0, &token);
    switch (c) {
      case 0:
        return argc;

      // 一般情况，解析到词
      case 'w':
        // 如果参数过多
        if (argc >= MAXARGS) {
          debugf("too many arguments\n");
          exit();
        }
        // 填入参数到argv
        argv[argc++] = token;
        break;

      // 遇到输入重定向的情况
      case '<':
        if (gettoken(0, &token) != 'w') {
          debugf("syntax error: < not followed by word\n");
          exit();
        }
        // 打开对应的文件
        fd = open(token, O_RDONLY);
        // 如果打开失败，则退出
        if(fd < 0) {
          debugf("open error\n");
          exit();
        }
        // 进行输入重定向
        dup(fd, 0);
        close(fd);
        break;

      // 遇到输出重定向的情况
      case '>':
        if (gettoken(0, &token) != 'w') {
          debugf("syntax error: > not followed by word\n");
          exit();
        }
        // 打开对应的文件
        fd = open(token, O_WRONLY);
        // 如果打开失败，则退出
        if(fd < 0) {
          debugf("open error\n");
          exit();
        }
        // 进行输出重定向
        dup(fd, 1);
        close(fd);
        break;

      // 遇到管道的情况
      case '|':
        // 创建一个管道
        ;int p[2];
        pipe(p);
        // 创建一个进程执行管道操作
        *rightpipe = fork();
        // 如果是子进程，即管道的右边
        if (*rightpipe == 0) {
          // 对输入重定向
          dup(p[0], 0);
          close(p[0]);
          close(p[1]);
          // 执行管道的右边
          return parsecmd(argv, rightpipe);
        }
        // 如果是父进程，即管道的左边
        else if (*rightpipe > 0) {
          // 对输出重定向
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
  // argc为参数阁主
  // argv为参数列表，形式为字符串
  char *argv[MAXARGS];

  int rightpipe = 0;
  // 解析参数到argv
  int argc = parsecmd(argv, &rightpipe);
  if (argc == 0) {
    return;
  }
  argv[argc] = 0;

  // 创建一个进程执行命令
  int child = spawn(argv[0], argv);
  // 关闭所有打开的文件
  close_all();
  // 如果是执行命令的子进程
  if (child >= 0) {
    wait(child);
  }
  // 如果是父进程
  else {
    debugf("spawn %s: %d\n", argv[0], child);
  }
  // 如果有管道，则等待执行完毕
  if (rightpipe) {
    wait(rightpipe);
  }
  // 退出
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

char buffer[1024];

void usage(void) {
  printf("usage: sh [-ix] [script-file]\n");
  exit();
}

int main(int argc, char **argv) {
  // 是否为交互式终端
  int interactive = iscons(0);
  // 是否要输出输入的命令
  int echocmds = 0;
  int func_info;

  // 打印反映信息
  printf("\n:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
  printf("::                                                         ::\n");
  printf("::                     MOS Shell 2024                      ::\n");
  printf("::                                                         ::\n");
  printf(":::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
  // 参数解析部分
  // "you are not expected to understand this"
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
  // 如果需要执行脚本，则关闭标准输入，改为文件作为输入
  if (argc == 1) {
    close(0);
    if ((func_info = open(argv[0], O_RDONLY)) < 0) {
      user_panic("open %s: %d", argv[0], func_info);
    }
    user_assert(func_info == 0);
  }

  // 在循环中不断读入命令行并进行处理
  for (;;) {
    // 在终端中，打印一个 $
    if (interactive) {
      printf("\n$ ");
    }
    // 读入一份命令到buffer
    readline(buffer, sizeof buffer);

    // 忽略以'#'开头的注释
    if (buffer[0] == '#') {
      continue;
    }
    // 在echocmds模式下输出读入的命令
    if (echocmds) {
      printf("# %s\n", buffer);
    }

    // 创建一个进程，执行命令
    int envid;
    if ((envid = fork()) < 0) {
      user_panic("fork: %d", envid);
    }
    // 对于父子进程
    // 子进程执行命令
    if (envid == 0) {
      runcmd(buffer);
      exit();
    }
    // 父进程等待子进程执行完毕
    else {
      wait(envid);
    }
  }
  return 0;
}
