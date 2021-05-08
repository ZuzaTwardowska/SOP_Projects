#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <signal.h>
#include <netdb.h>
#include <fcntl.h>
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))
#define BACKLOG 3
#define Q_LENGTH 2000
struct connections
{
    int free;
    struct sockaddr_in addr;
    int fd;
    int question;
    int offset;
};
struct questions
{
    char line[Q_LENGTH];
    int length;
};
volatile sig_atomic_t last_signal = 0;
volatile sig_atomic_t death = 0;
volatile sig_atomic_t acceptNew = 0;
void sigalrm_handler(int sig)
{
    last_signal = sig;
}
void sigint_handler(int sig)
{
    death = sig;
}
void sigusr_handler(int sig)
{
    acceptNew = sig;
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
void usage(char *name)
{
    fprintf(stderr, "USAGE: %s address port max_number_of_clients file\n", name);
}
ssize_t bulk_read(int fd, char *buf, size_t count)
{
    int c;
    size_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0)
            return c;
        if (0 == c)
            return len;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}
ssize_t bulk_write(int fd, char *buf, size_t count)
{
    int c;
    size_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0)
            return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}
void EndQuestion(struct connections *conn,int i)
{
    char end=0;
    if (TEMP_FAILURE_RETRY(write(conn[i].fd, &end, sizeof(char))) < 0)
    {
        if (errno == EPIPE || errno == ECONNRESET)
        {
            conn[i].free = 1;
            if(close(conn[i].fd)) ERR("close");
            return;
        }
        ERR("write:");
    }
}
void communicate(struct connections *conn, int maxClient, int lines, struct questions *q)
{
    int buf_len = 0, count, a;
    char c;
    for (int i = 0; i < maxClient; i++)
        if (conn[i].free == 0)
        {
            if (conn[i].offset == 0)
                conn[i].question = rand() % lines;
            if (conn[i].offset == q[conn[i].question].length)
            {
                if ((a = TEMP_FAILURE_RETRY(read(conn[i].fd, &c, sizeof(char)))) < 1)
                {
                    if (a == 0)
                    {
                        conn[i].free = 1;
                        if(close(conn[i].fd)) ERR("close");
                        continue;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        continue;
                    ERR("read from client");
                }
                else
                {
                    conn[i].offset = 0;
                    printf("Client %d responded to question:%s with %c\n\n", i, q[conn[i].question].line, c);
                }
                continue;
            }
            buf_len = rand() % (q[conn[i].question].length - conn[i].offset) + 1;
            if ((count = TEMP_FAILURE_RETRY(write(conn[i].fd, q[conn[i].question].line + conn[i].offset, buf_len))) < 0)
            {
                if (errno == EPIPE || errno == ECONNRESET)
                {
                    conn[i].free = 1;
                    if(close(conn[i].fd)) ERR("close");
                    continue;
                }
                ERR("write:");
            }
            conn[i].offset += count;
            if (conn[i].offset == q[conn[i].question].length)
                EndQuestion(conn,i);
        }
}
int findIndex(struct sockaddr_in addr, struct connections *con, int maxClient)
{
    int i;
    for (i = 0; i < maxClient; i++)
    {
        if (con[i].free == 0)
        {
            if (0 == memcmp(&addr, &(con[i].addr), sizeof(struct sockaddr_in)))
            {
                return -1;
            }
        }
    }
    for (i = 0; i < maxClient; i++)
    {
        if (con[i].free == 1)
            return i;
    }
    return -2;
}
void add_new_client(int sfd, struct connections *conn, int maxClient)
{
    int nfd;
    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(addr);
    if ((nfd = TEMP_FAILURE_RETRY(accept(sfd, &addr, &addr_size))) < 0)
    {
        if (EAGAIN == errno || EWOULDBLOCK == errno)
            return;
        ERR("accept");
    }
    int i = findIndex(addr, conn, maxClient);
    if (i == -1)
        return;
    if (i < 0)
    {
        if (bulk_write(nfd, (char *)"NIE\n", sizeof(char[4])) < 0 && errno != EPIPE)
            ERR("write:");
        if (TEMP_FAILURE_RETRY(close(nfd)) < 0)
            ERR("close");
        return;
    }
    int new_flags = fcntl(nfd, F_GETFL) | O_NONBLOCK;
    fcntl(nfd, F_SETFL, new_flags);
    conn[i].addr = addr;
    conn[i].fd = nfd;
    conn[i].free = 0;
    conn[i].question = 0;
    conn[i].offset = 0;
}
void EndAllConnections(struct connections *conn, int maxClient)
{
    for (int i = 0; i < maxClient; i++)
    {
        if (conn[i].free == 0)
        {
            if (bulk_write(conn[i].fd, (char *)"Koniec", sizeof(char[9])) < 0 && errno != EPIPE)
                ERR("write");
        }
        if (conn[i].free == 0 && close(conn[i].fd))
            ERR("close");
    }
}
void SetTimer()
{
    struct itimerval ts;
    memset(&ts, 0, sizeof(struct itimerval));
    ts.it_value.tv_usec = 330000;
    setitimer(ITIMER_REAL, &ts, NULL);
}
void AlarmAction(struct connections *conn, int maxClient, struct questions *q, int lines)
{
    last_signal = 0;
    communicate(conn, maxClient, lines, q);
    SetTimer();
}
void SetMask(sigset_t *mask, sigset_t *oldmask)
{
    sigemptyset(mask);
    sigaddset(mask, SIGINT);
    sigaddset(mask, SIGALRM);
    sigaddset(mask, SIGUSR1);
    sigprocmask(SIG_BLOCK, mask, oldmask);
}
void ReadQuestions(struct questions *questions, int file, int lines)
{
    int len = 0, count, i = 0;
    char c;
    for (int j = 0; j < lines; j++)
    {
        memset(questions[j].line, 0, sizeof(Q_LENGTH));
    }
    if (lseek(file, 0, SEEK_SET) == (off_t)-1)
        ERR("lseek");
    do
    {
        if ((count = bulk_read(file, &c, sizeof(char))) < 1)
        {
            if (count != 0)
                ERR("read from file");
        }
        if (count != 0)
        {
            questions[i].line[len++] = c;
            if (c == '?')
            {
                questions[i].line[len++] = 0;
                questions[i++].length = len;
                len = 0;
            }
        }
    } while (count != 0);
    if (TEMP_FAILURE_RETRY(close(file)) < 0)
        ERR("close");
}
int CountQuestions(int file)
{
    int lines = 0;
    char c;
    int count;
    do
    {
        if ((count = bulk_read(file, &c, sizeof(char))) < 1)
        {
            if (count != 0)
                ERR("read from file");
        }
        if (count != 0)
        {
            if (c == '?')
                lines++;
        }
    } while (count != 0);
    return lines;
}
int doServer(int fd, int maxClient, int file)
{
    struct connections *conn = (struct connections *)malloc(sizeof(struct connections) * maxClient);
    int lines = CountQuestions(file), stopListen = 0;
    struct questions *questions = (struct questions *)malloc(sizeof(struct questions) * lines);
    ReadQuestions(questions, file, lines);
    fd_set base_rfds, rfds;
    sigset_t mask, oldmask;
    FD_ZERO(&base_rfds);
    FD_SET(fd, &base_rfds);
    SetMask(&mask, &oldmask);
    for (int i = 0; i < maxClient; i++)
        conn[i].free = 1;
    if (sethandler(sigalrm_handler, SIGALRM))
        ERR("Seting SIGALRM:");
    SetTimer();
    while (death == 0)
    {
        if (acceptNew != 0)
        {
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
            if (stopListen == 0)
            {
                stopListen = 1;
                if (TEMP_FAILURE_RETRY(close(fd)) < 0)
                    ERR("close");
            }
            break;
        }
        rfds = base_rfds;
        if (pselect(fd + 1, &rfds, NULL, NULL, NULL, &oldmask) > 0)
        {
            if (FD_ISSET(fd, &rfds))
                add_new_client(fd, conn, maxClient);
        }
        else
        {
            if (EINTR == errno)
            {
                if (last_signal == SIGALRM)
                    AlarmAction(conn, maxClient, questions, lines);
            }
            else
                ERR("pselect");
        }
    }
    while (death == 0)
    {
        sigsuspend(&oldmask);
        if (last_signal == SIGALRM)
            AlarmAction(conn, maxClient, questions, lines);
    }
    sigprocmask(SIG_UNBLOCK, &mask, NULL);
    EndAllConnections(conn, maxClient);
    return stopListen;
}
struct sockaddr_in make_address(char *address, char *port)
{
    int ret;
    struct sockaddr_in addr;
    struct addrinfo *result;
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    if ((ret = getaddrinfo(address, port, &hints, &result)))
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        exit(EXIT_FAILURE);
    }
    addr = *(struct sockaddr_in *)(result->ai_addr);
    freeaddrinfo(result);
    return addr;
}
int make_socket(int domain, int type)
{
    int sock;
    sock = socket(domain, type, 0);
    if (sock < 0)
        ERR("socket");
    return sock;
}
int bind_tcp_socket(char *port, char *name)
{
    struct sockaddr_in addr;
    int socketfd, t = 1;
    socketfd = make_socket(PF_INET, SOCK_STREAM);
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr = make_address(name, port);
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
        ERR("setsockopt");
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR("bind");
    if (listen(socketfd, BACKLOG) < 0)
        ERR("listen");
    return socketfd;
}
int main(int argc, char **argv)
{
    srand(getpid());
    int fd, file, maxClient;
    if (sethandler(sigint_handler, SIGINT))
        ERR("Seting SIGINT:");
    if (sethandler(sigusr_handler, SIGUSR1))
        ERR("Seting SIGUSR:");
    if (argc != 5)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    maxClient = atoi(argv[3]);
    if ((file = TEMP_FAILURE_RETRY(open(argv[4], O_RDONLY))) < 0)
        ERR("open");
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE:");
    fd = bind_tcp_socket(argv[2], argv[1]);
    int new_flags = fcntl(fd, F_GETFL) | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flags);
    if (doServer(fd, maxClient, file) == 0)
    {
        if (TEMP_FAILURE_RETRY(close(fd)) < 0)
            ERR("close");
    }
    fprintf(stderr, "Server has terminated.\n");
    return EXIT_SUCCESS;
}
