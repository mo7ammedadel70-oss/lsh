/*
 * lsh.c — Little Shell
 * Original by Stephen Brennan
 * Extended with: pwd, echo, history, env
 *
 * HOW TO COMPILE:
 *   gcc -o lsh lsh.c
 *
 * HOW TO RUN:
 *   ./lsh
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>    /* getcwd, chdir, fork, execvp */
#include <sys/wait.h>  /* waitpid */

/* ============================================================
 *  CONSTANTS
 * ============================================================ */
#define LSH_RL_BUFSIZE   1024   /* max chars per input line     */
#define LSH_TOK_BUFSIZE   64   /* max tokens per line          */
#define LSH_TOK_DELIM  " \t\r\n\a"
#define HISTORY_MAX       100   /* max commands kept in history  */

/* ============================================================
 *  HISTORY  (global state)
 * ============================================================ */
static char *history[HISTORY_MAX];
static int   history_count = 0;

/* Add a command string to the history list */
static void history_add(const char *line)
{
    if (history_count < HISTORY_MAX) {
        history[history_count++] = strdup(line);
    } else {
        /* Ring-buffer: drop oldest, shift everything left */
        free(history[0]);
        memmove(history, history + 1,
                (HISTORY_MAX - 1) * sizeof(char *));
        history[HISTORY_MAX - 1] = strdup(line);
    }
}

/* Free all history entries (called at exit) */
static void history_free(void)
{
    for (int i = 0; i < history_count; i++) {
        free(history[i]);
        history[i] = NULL;
    }
    history_count = 0;
}

/* ============================================================
 *  FORWARD DECLARATIONS — builtins
 * ============================================================ */
int lsh_cd(char **args);
int lsh_help(char **args);
int lsh_exit(char **args);
int lsh_pwd(char **args);
int lsh_echo(char **args);
int lsh_history(char **args);
int lsh_env(char **args);

/* ============================================================
 *  BUILTIN TABLE
 *  Add every builtin name here AND its function pointer below.
 * ============================================================ */
char *builtin_str[] = {
    "cd",
    "help",
    "exit",
    "pwd",
    "echo",
    "history",
    "env"
};

int (*builtin_func[])(char **) = {
    &lsh_cd,
    &lsh_help,
    &lsh_exit,
    &lsh_pwd,
    &lsh_echo,
    &lsh_history,
    &lsh_env
};

int lsh_num_builtins(void)
{
    return sizeof(builtin_str) / sizeof(char *);
}

/* ============================================================
 *  BUILTIN IMPLEMENTATIONS
 * ============================================================ */

/* ---- cd ---- */
int lsh_cd(char **args)
{
    if (args[1] == NULL) {
        fprintf(stderr, "lsh: cd: expected argument\n");
    } else if (chdir(args[1]) != 0) {
        perror("lsh: cd");
    }
    return 1;
}

/* ---- help ---- */
int lsh_help(char **args)
{
    (void)args; /* unused */
    printf("=== Little Shell (lsh) ===\n");
    printf("Built-in commands:\n");
    for (int i = 0; i < lsh_num_builtins(); i++) {
        printf("  %s\n", builtin_str[i]);
    }
    printf("Any other command is run as an external program.\n");
    return 1;
}

/* ---- exit ---- */
int lsh_exit(char **args)
{
    (void)args;
    history_free();
    return 0; /* returning 0 tells the main loop to stop */
}

/* ---- pwd ---- */
int lsh_pwd(char **args)
{
    (void)args;
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("lsh: pwd");
    }
    return 1;
}

/* ---- echo ---- */
int lsh_echo(char **args)
{
    /* args[0] == "echo", actual text starts at args[1] */
    if (args[1] == NULL) {
        /* echo with no arguments → just print a blank line */
        printf("\n");
        return 1;
    }
    for (int i = 1; args[i] != NULL; i++) {
        printf("%s", args[i]);
        if (args[i + 1] != NULL)
            printf(" ");
    }
    printf("\n");
    return 1;
}

/* ---- history ---- */
int lsh_history(char **args)
{
    (void)args;
    if (history_count == 0) {
        printf("(no commands in history)\n");
        return 1;
    }
    for (int i = 0; i < history_count; i++) {
        printf("%4d  %s\n", i + 1, history[i]);
    }
    return 1;
}

/* ---- env ---- */
/* environ is a POSIX global: the list of "KEY=VALUE" strings */
extern char **environ;

int lsh_env(char **args)
{
    (void)args;
    for (char **ep = environ; *ep != NULL; ep++) {
        printf("%s\n", *ep);
    }
    return 1;
}

/* ============================================================
 *  LAUNCH EXTERNAL PROGRAM
 * ============================================================ */
int lsh_launch(char **args)
{
    pid_t pid;
    int   status;

    pid = fork();
    if (pid == 0) {
        /* Child process */
        if (execvp(args[0], args) == -1) {
            perror("lsh");
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("lsh: fork");
    } else {
        /* Parent: wait for child */
        do {
            waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }
    return 1;
}

/* ============================================================
 *  EXECUTE (builtin check → launch)
 * ============================================================ */
int lsh_execute(char **args)
{
    if (args[0] == NULL) {
        /* Empty command — do nothing */
        return 1;
    }

    for (int i = 0; i < lsh_num_builtins(); i++) {
        if (strcmp(args[0], builtin_str[i]) == 0) {
            return (*builtin_func[i])(args);
        }
    }

    return lsh_launch(args);
}

/* ============================================================
 *  READ A LINE FROM STDIN
 * ============================================================ */
char *lsh_read_line(void)
{
    int  bufsize = LSH_RL_BUFSIZE;
    int  pos     = 0;
    char *buffer = malloc(bufsize);
    int  c;

    if (!buffer) {
        fprintf(stderr, "lsh: allocation error\n");
        exit(EXIT_FAILURE);
    }

    while (1) {
        c = getchar();

        if (c == EOF || c == '\n') {
            buffer[pos] = '\0';
            return buffer;
        }

        buffer[pos++] = (char)c;

        if (pos >= bufsize) {
            bufsize += LSH_RL_BUFSIZE;
            buffer = realloc(buffer, bufsize);
            if (!buffer) {
                fprintf(stderr, "lsh: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }
    }
}

/* ============================================================
 *  SPLIT LINE INTO TOKENS
 * ============================================================ */
char **lsh_split_line(char *line)
{
    int   bufsize = LSH_TOK_BUFSIZE;
    int   pos     = 0;
    char **tokens = malloc(bufsize * sizeof(char *));
    char *token;

    if (!tokens) {
        fprintf(stderr, "lsh: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, LSH_TOK_DELIM);
    while (token != NULL) {
        tokens[pos++] = token;

        if (pos >= bufsize) {
            bufsize += LSH_TOK_BUFSIZE;
            tokens = realloc(tokens, bufsize * sizeof(char *));
            if (!tokens) {
                fprintf(stderr, "lsh: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, LSH_TOK_DELIM);
    }
    tokens[pos] = NULL;
    return tokens;
}

/* ============================================================
 *  MAIN LOOP
 * ============================================================ */
void lsh_loop(void)
{
    char  *line;
    char **args;
    int    status;

    do {
        printf("lsh> ");
        fflush(stdout);

        line = lsh_read_line();

        /* Save non-empty lines to history BEFORE executing */
        if (line[0] != '\0') {
            history_add(line);
        }

        args   = lsh_split_line(line);
        status = lsh_execute(args);

        free(line);
        free(args);
    } while (status);
}

/* ============================================================
 *  ENTRY POINT
 * ============================================================ */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    lsh_loop();
    return EXIT_SUCCESS;
}
