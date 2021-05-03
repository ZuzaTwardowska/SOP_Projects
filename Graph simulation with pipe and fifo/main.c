#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>

#define ERR(source) (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))
volatile sig_atomic_t last_signal = 0;
void usage(char *name)
{
    fprintf(stderr, "USAGE: %s -number of vertex\n", name);
    exit(EXIT_FAILURE);
}
int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}
void signal_handler(int sig)
{
    if (last_signal == sig)
        return;
    last_signal = sig;
    if (kill(0, SIGINT) < 0)
        ERR("kill");
}
void signal_chld_handler(int sig)
{
    last_signal = sig;
}
void sigchld_handler(int sig)
{
    pid_t pid;
    for (;;)
    {
        pid = waitpid(0, NULL, WNOHANG);
        if (0 == pid)
            return;
        if (0 >= pid)
        {
            if (ECHILD == errno)
                return;
            ERR("waitpid");
        }
    }
}
int get_address(char *query)
{
    int val = 0;
    int count_spaces = 0;
    for (int i = 0; query[i] != 0; i++)
    {
        if (query[i] == ' ')
        {
            count_spaces++;
            if (count_spaces == 2)
                break;
            continue;
        }
        if (isdigit(query[i]) == 0)
            continue;
        if (count_spaces == 1)
        {
            val *= 10;
            val += query[i] - '0';
        }
    }
    return val;
}
void read_from_fifo(int fifo, int *fds, int n)
{
    ssize_t count;
    char query[PIPE_BUF];
    int w[n];
    for (int i = 0; i < n; i++)
    {
        w[i] = fds[2 * i + 1];
    }
    while (1)
    {
        if (SIGINT == last_signal)
        {
            return;
        }
        memset(query, 0, PIPE_BUF);
        if ((count = read(fifo, query, PIPE_BUF)) < 0)
            ERR("read");
        if (count == 0)
        {
            if (kill(getpid(), SIGINT) < 0)
                ERR("kill");
            return;
        }
        if (count > 0)
        {
            query[count] = 0;
            if (strcmp(query, "print\n") == 0)
            {
                for (int i = 0; i < n; i++)
                {
                    if (write(w[i], query, PIPE_BUF) < 0)
                        ERR("write to children");
                }
            }
            else if (query[0] == 'a' && query[1] == 'd' && query[2] == 'd')
            {
                int x = get_address(query);
                if (x >= n)
                {
                    printf("Wierzcholek %d nie istnieje w grafie\n", x);
                    continue;
                }
                if (write(w[x], query, PIPE_BUF) < 0)
                    ERR("write to children");
            }
            else if (query[0] == 'c' && query[1] == 'o' && query[2] == 'n' && query[3] == 'n')
            {
                int x = get_address(query);
                if (x >= n)
                {
                    printf("Wierzcholek %d nie istnieje w grafie\n", x);
                    continue;
                }
                if (write(w[x], query, PIPE_BUF) < 0)
                    ERR("write to children");
            }
        }
    };
}
int get_destination(char *query)
{
    int val = 0;
    int count_spaces = 0;
    for (int i = 0; query[i] != 0; i++)
    {
        if (query[i] == ' ')
        {
            count_spaces++;
            continue;
        }
        if (isdigit(query[i]) == 0)
            continue;
        if (count_spaces == 2)
        {
            val *= 10;
            val += query[i] - '0';
        }
    }
    return val;
}
int find_connection(int y, int *perm, int *w, int n, int num, int r)
{
    if (perm[y] == 1)
        return 1;
    ssize_t count;
    char query[PIPE_BUF];
    memset(query,0,PIPE_BUF);
    for (int i = 0; i < n; i++)
    {
        if(i==num) continue;
        sprintf(query,"find %d %d",num,y);
        if (perm[i] == 1)
        {
            if (write(w[i], query, PIPE_BUF) < 0)ERR("write to children");
            if (SIGINT == last_signal)
            {
                return 0;
            }
            memset(query, 0, PIPE_BUF);
            if ((count = read(r, &query, PIPE_BUF)) < 0)
            {
                if (SIGINT == last_signal)
                {
                    return 0;
                }
                ERR("read");
            }
            query[count] = 0;
            if (strcmp(query, "true\n") == 0)
                return 1;
            if(query[0] == 'f' && query[1] == 'i' && query[2] == 'n' && query[3] == 'd')
            {
                int x = get_address(query);
                if (write(w[x], "false\n", PIPE_BUF) < 0)
                ERR("write to children");
                return 0;
            }
        }
    }
    return 0;
}
void work(int *fds, int n, int num)
{
    int r = fds[2 * num];
    int w[n], perm[n];
    for (int i = 0; i < n; i++)
    {
        perm[i] = 0;
        w[i] = fds[2 * i + 1];
    }
    ssize_t count;
    char query[PIPE_BUF];

    while (1)
    {
        if (SIGINT == last_signal)
        {
            return;
        }
        memset(query, 0, PIPE_BUF);
        if ((count = read(r, &query, PIPE_BUF)) < 0)
        {
            if (SIGINT == last_signal)
            {
                return;
            }
            ERR("read");
        }
        query[count] = 0;
        if (strcmp(query, "print\n") == 0)
        {
            for (int i = 0; i < n; i++)
            {
                if (perm[i] != 0)
                    printf("Wierzcholek %d polaczony z wierzcholkiem %d\n", num, i);
            }
        }
        else if (query[0] == 'a' && query[1] == 'd' && query[2] == 'd')
        {
            int y = get_destination(query);
            if (y >= n)
            {
                printf("Wierzcholek %d nie istnieje w grafie\n", y);
                continue;
            }
            perm[y] = 1;
        }
        else if (query[0] == 'c' && query[1] == 'o' && query[2] == 'n' && query[3] == 'n')
        {
            int y = get_destination(query);
            if (y >= n)
            {
                printf("Wierzcholek %d nie istnieje w grafie\n", y);
                continue;
            }
            int found = find_connection(y, perm, w, n, num, r);
            if (found == 1)
            {
                printf("Istnieje polaczenie miedzy %d i %d\n", num, y);
            }
            else
            {
                printf("Nie istnieje polaczenie miedzy %d i %d\n", num, y);
            }
        }
        else if (query[0] == 'f' && query[1] == 'i' && query[2] == 'n' && query[3] == 'd')
        {
            int y = get_destination(query);
            int x = get_address(query);
            int found = find_connection(y, perm, w, n, num, r);
            if (found == 1)
            {
                if (write(w[x], "true\n", PIPE_BUF) < 0)
                ERR("write to children");
            }
            else
            {
                if (write(w[x], "false\n", PIPE_BUF) < 0)
                ERR("write to children");
            }
        }
    }
}
void create_children(int *fds, int n)
{
    for (int i = 0; i < n; i++)
    {
        switch (fork())
        {
        case -1:
            ERR("fork");
        case 0:
            if (sethandler(signal_chld_handler, SIGINT))
                ERR("setting SIGINT handler");
            for (int j = 0; j < n; j++)
            {
                if (j == i)
                    continue;
                if (close(fds[2 * j]))
                    ERR("close");
                fds[2 * j] = 0;
            }
            work(fds, n, i);
            for (int j = 0; j < 2 * n; j++)
            {
                if (fds[j] != 0)
                    if (close(fds[j]))
                        ERR("close");
            }
            free(fds);
            exit(EXIT_SUCCESS);
        }
    }
}
int main(int argc, char **argv)
{
    if (argc != 2)
        usage(argv[0]);
    int fifo;
    int *fds;
    int n, R[2];
    n = atoi(argv[1]);
    fds = (int *)malloc(sizeof(int) * (2 * n));
    if (!fds)
        ERR("malloc");
    for (int i = 0; i < n; i++)
    {
        if (pipe(R))
            ERR("pipe");
        fds[2 * i] = R[0];
        fds[2 * i + 1] = R[1];
    }
    if (sethandler(SIG_IGN, SIGINT))
        ERR("setting SIGINT handler");
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("setting SIGPIPE handler");
    if (sethandler(sigchld_handler, SIGCHLD))
        ERR("setting SIGCHLD handler");
    create_children(fds, n);
    for (int i = 0; i < n; i++)
    {
        if (close(fds[2 * i]))
            ERR("close");
    }
    if (sethandler(signal_handler, SIGINT))
        ERR("setting SIGINT handler");
    if (mkfifo("graph.fifo", S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) < 0)
    {
        if (errno != EEXIST)
            ERR("create fifo");
        else
        {
            if (unlink("graph.fifo") < 0)
                ERR("unlink");
            if (mkfifo("graph.fifo", S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) < 0)
                if (errno != EEXIST)
                    ERR("create fifo");
        }
    }
    if ((fifo = open("graph.fifo", O_RDONLY)) < 0)
        ERR("open");
    read_from_fifo(fifo, fds, n);

    if (close(fifo) < 0)
        ERR("close");
    if (unlink("graph.fifo") < 0)
        ERR("unlink");
    for (int i = 0; i < n; i++)
    {
        if (close(fds[2 * i + 1]))
            ERR("close");
    }
    free(fds);
    while (wait(NULL) > 0)
        ;
    return EXIT_SUCCESS;
}