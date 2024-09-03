// Shell.

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// Parsed command representation
#define EXEC 1
#define REDIR 2
#define PIPE 3
#define LIST 4
#define BACK 5

#define MAXARGS 10

struct cmd {
    int type;
};

struct exec_cmd {
    int type;
    char* argv[MAXARGS];
    char* eargv[MAXARGS];
};

struct redir_cmd {
    int type;
    struct cmd* cmd;
    char* file;
    char* efile;
    int mode;
    int fd;
};

struct pipe_cmd {
    int type;
    struct cmd* left;
    struct cmd* right;
};

struct list_cmd {
    int type;
    struct cmd* left;
    struct cmd* right;
};

/**
 * @brief 后台命令
 *
 */
struct back_cmd {
    int type;
    struct cmd* cmd;
};

int fork1(void); // Fork but panics on failure.
void panic(char*);
struct cmd* parse_cmd(char*);
void run_cmd(struct cmd*) __attribute__((noreturn));

// Execute cmd.  Never returns.
void run_cmd(struct cmd* cmd)
{
    if (cmd == 0) {
        exit(1);
    }

    switch (cmd->type) {
    default:
        panic("run_cmd");

    case EXEC:
        struct exec_cmd* ecmd = (struct exec_cmd*)cmd;
        if (ecmd->argv[0] == 0) {
            exit(1);
        }
        exec(ecmd->argv[0], ecmd->argv);
        fprintf(2, "exec %s failed\n", ecmd->argv[0]);
        break;

    case REDIR:
        struct redir_cmd* rcmd = (struct redir_cmd*)cmd;
        close(rcmd->fd);
        if (open(rcmd->file, rcmd->mode) < 0) {
            fprintf(2, "open %s failed\n", rcmd->file);
            exit(1);
        }
        run_cmd(rcmd->cmd);
        break;

    case LIST:
        struct list_cmd* lcmd = (struct list_cmd*)cmd;
        if (fork1() == 0) {
            run_cmd(lcmd->left);
        }
        wait(0);
        run_cmd(lcmd->right);
        break;

    case PIPE:
        struct pipe_cmd* pcmd = (struct pipe_cmd*)cmd;
        int p[2];
        if (pipe(p) < 0) {
            panic("pipe");
        }
        if (fork1() == 0) {
            close(1);
            dup(p[1]);
            close(p[0]);
            close(p[1]);
            run_cmd(pcmd->left);
        }
        if (fork1() == 0) {
            close(0);
            dup(p[0]);
            close(p[0]);
            close(p[1]);
            run_cmd(pcmd->right);
        }
        close(p[0]);
        close(p[1]);
        wait(0);
        wait(0);
        break;

    case BACK:
        struct back_cmd* bcmd = (struct back_cmd*)cmd;
        if (fork1() == 0) {
            run_cmd(bcmd->cmd);
        }
        break;
    }
    exit(0);
}

int get_cmd(char* buf, int nbuf)
{
    write(2, "$ ", 2);
    memset(buf, 0, nbuf);
    gets(buf, nbuf);
    if (buf[0] == 0) // EOF
    {
        return -1;
    }
    return 0;
}

int main(void)
{
    static char buf[100];
    int fd;

    // Ensure that three file descriptors are open.
    while ((fd = open("console", O_RDWR)) >= 0) {
        if (fd >= 3) {
            close(fd);
            break;
        }
    }

    // Read and run input commands.
    while (get_cmd(buf, sizeof(buf)) >= 0) {
        if (buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' ') {
            // Chdir must be called by the parent, not the child.
            buf[strlen(buf) - 1] = 0; // chop \n
            if (chdir(buf + 3) < 0)
                fprintf(2, "cannot cd %s\n", buf + 3);
            continue;
        }
        // 子进程
        if (fork1() == 0) {
            run_cmd(parse_cmd(buf));
        }
        wait(0);
    }
    exit(0);
}

void panic(char* s)
{
    fprintf(2, "%s\n", s);
    exit(1);
}

int fork1(void)
{
    int pid;

    pid = fork();
    if (pid == -1)
        panic("fork");
    return pid;
}

// PAGEBREAK!
//  Constructors

struct cmd* exec_cmd(void)
{
    struct exec_cmd* cmd;

    cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = EXEC;
    return (struct cmd*)cmd;
}

struct cmd* redir_cmd(struct cmd* subcmd, char* file, char* efile, int mode, int fd)
{

    struct redir_cmd* cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = REDIR;
    cmd->cmd = subcmd;
    cmd->file = file;
    cmd->efile = efile;
    cmd->mode = mode;
    cmd->fd = fd;
    return (struct cmd*)cmd;
}

struct cmd* pipe_cmd(struct cmd* left, struct cmd* right)
{
    struct pipe_cmd* cmd;

    cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = PIPE;
    cmd->left = left;
    cmd->right = right;
    return (struct cmd*)cmd;
}

struct cmd* list_cmd(struct cmd* left, struct cmd* right)
{
    struct list_cmd* cmd;

    cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = LIST;
    cmd->left = left;
    cmd->right = right;
    return (struct cmd*)cmd;
}

/**
 * @brief (C还是可以区分: struct 和 func 符号名相同)
 *
 * @param subcmd
 * @return struct cmd*
 */
struct cmd* back_cmd(struct cmd* subcmd)
{

    struct back_cmd* cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = BACK;
    cmd->cmd = subcmd;
    return (struct cmd*)cmd;
}

// PAGE BREAK!
//  Parsing

char whitespace[] = " \t\r\n\v";

/**
 * @brief Get the token object (词法分析, 自动机)
 *
 * @param ps
 * @param es
 * @param q begin of token
 * @param eq  end of token
 * @return int
 */
int get_token(char** ps, char* es, char** q, char** eq)
{
    char* s = *ps;
    while (s < es && strchr(whitespace, *s)) {
        s++;
    }
    if (q) {
        *q = s;
    }
    int ret = *s;
    switch (*s) {
    case 0:
        break;
    case '|':
    case '(':
    case ')':
    case ';':
    case '&':
    case '<':
        s++;
        break;
    case '>':
        s++;
        if (*s == '>') {
            ret = '+';
            s++;
        }
        break;
    default:
        ret = 'a'; // 标记为 'a'
        while (s < es && !strchr(whitespace, *s) && !strchr("<|>&;()", *s)) {
            s++;
        }
        break;
    }
    if (eq) {
        *eq = s;
    }

    while (s < es && strchr(whitespace, *s)) {
        s++;
    }
    *ps = s;
    return ret;
}

/**
 * @param ps 用于更新实参: 指向第一个非空白符
 * @param es end of s
 * @return int 如果第一个非空白符不是 \0, 并且 s 在 toks 中出现 -> return true
 */
int peek(char** ps, char* es, char* toks)
{
    char* s = *ps;
    // 获取到第一个非空白字符
    while (s < es && strchr(whitespace, *s)) {
        s++;
    }
    *ps = s;
    return *s && strchr(toks, *s);
}

struct cmd* parse_line(char**, char*);
struct cmd* parse_pipe(char**, char*);
struct cmd* parse_exec(char**, char*);
struct cmd* nul_terminate(struct cmd*);

struct cmd* parse_cmd(char* s)
{
    char* es = s + strlen(s); // end of s
    struct cmd* cmd = parse_line(&s, es);
    peek(&s, es, "");
    if (s != es) { // 没有解析完 cmd -> fatal
        fprintf(2, "leftovers: %s\n", s); // stderr
        panic("syntax");
    }
    nul_terminate(cmd);
    return cmd;
}

struct cmd* parse_line(char** ps, char* es)
{
    struct cmd* cmd = parse_pipe(ps, es); // 解析一行的时候, 要先看一下有没有 pipe
    while (peek(ps, es, "&")) {
        get_token(ps, es, 0, 0);
        cmd = back_cmd(cmd); // <back> & <front>
    }
    if (peek(ps, es, ";")) { // 根据 ';' 分成多行
        get_token(ps, es, 0, 0);
        cmd = list_cmd(cmd, parse_line(ps, es)); // 命令链, 递归的解析
    }
    return cmd;
}

struct cmd* parse_pipe(char** ps, char* es)
{
    struct cmd* cmd = parse_exec(ps, es);
    if (peek(ps, es, "|")) {
        get_token(ps, es, 0, 0);
        // 递归的看, 是不是有多个 pipe, 链式传递
        cmd = pipe_cmd(cmd, parse_pipe(ps, es));
    }
    return cmd;
}

struct cmd* parse_redirs(struct cmd* cmd, char** ps, char* es)
{
    while (peek(ps, es, "<>")) {
        int tok = get_token(ps, es, 0, 0);
        char *q, *eq;
        if (get_token(ps, es, &q, &eq) != 'a')
            panic("missing file for redirection");
        switch (tok) {
        case '<':
            cmd = redir_cmd(cmd, q, eq, O_RDONLY, 0);
            break;
        case '>':
            cmd = redir_cmd(cmd, q, eq, O_WRONLY | O_CREATE | O_TRUNC, 1);
            break;
        case '+': // >>
            cmd = redir_cmd(cmd, q, eq, O_WRONLY | O_CREATE, 1);
            break;
        }
    }
    return cmd;
}

struct cmd* parse_block(char** ps, char* es)
{
    if (!peek(ps, es, "(")) {
        panic("parse_block");
    }
    get_token(ps, es, 0, 0);
    struct cmd* cmd = parse_line(ps, es);
    if (!peek(ps, es, ")")) {
        panic("syntax - missing )");
    }
    get_token(ps, es, 0, 0);
    cmd = parse_redirs(cmd, ps, es);
    return cmd;
}

struct cmd* parse_exec(char** ps, char* es)
{
    if (peek(ps, es, "(")) {
        return parse_block(ps, es);
    }
    struct cmd* ret = exec_cmd();
    struct exec_cmd* cmd = (struct exec_cmd*)ret;
    int argc = 0;
    ret = parse_redirs(ret, ps, es);
    while (!peek(ps, es, "|)&;")) {
        int tok;
        char *q, *eq;
        if ((tok = get_token(ps, es, &q, &eq)) == 0) {
            break;
        }
        if (tok != 'a') {
            panic("syntax");
        }
        cmd->argv[argc] = q;
        cmd->eargv[argc] = eq;
        argc++;
        if (argc >= MAXARGS) {
            panic("too many args");
        }
        ret = parse_redirs(ret, ps, es);
    }
    cmd->argv[argc] = 0;
    cmd->eargv[argc] = 0;
    return ret;
}

// NUL-terminate all the counted strings.
struct cmd* nul_terminate(struct cmd* cmd)
{
    if (cmd == 0) {
        return 0;
    }

    switch (cmd->type) {
    case EXEC:
        struct exec_cmd* ecmd = (struct exec_cmd*)cmd;
        for (int i = 0; ecmd->argv[i]; i++) {
            *ecmd->eargv[i] = 0;
        }
        break;

    case REDIR:
        struct redir_cmd* rcmd = (struct redir_cmd*)cmd;
        nul_terminate(rcmd->cmd); // 重定向？
        *rcmd->efile = 0;
        break;

    case PIPE:
        struct pipe_cmd* pcmd = (struct pipe_cmd*)cmd;
        nul_terminate(pcmd->left);
        nul_terminate(pcmd->right);
        break;

    case LIST:
        struct list_cmd* lcmd = (struct list_cmd*)cmd;
        nul_terminate(lcmd->left);
        nul_terminate(lcmd->right);
        break;

    case BACK:
        struct back_cmd* bcmd = (struct back_cmd*)cmd;
        nul_terminate(bcmd->cmd);
        break;
    }
    return cmd;
}
