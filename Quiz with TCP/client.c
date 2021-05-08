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
#define A_LENGTH 50

struct server
{
    char *addr;
    int port;
    char line[Q_LENGTH];
    int offset;
    int waiting;
    int fd;
    int free;
};
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
    fprintf(stderr, "USAGE: %s pairs(address port)\n", name);
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
int FindMaxfd(int serversNum, struct server *server)
{
    int max = 0;
    for (int i = 0; i < serversNum; i++)
    {
        if (server[i].free == 1)
            continue;
        if (server[i].fd > max)
            max = server[i].fd;
    }
    return max;
}
int FindWaiting(int serversNum, struct server *server)
{
    for (int i = 0; i < serversNum; i++)
    {
        if (server[i].free == 0 && server[i].waiting == 1)
        {
            return i;
        }
    }
    return -1;
}
void connEnd(int *serversNum, struct server *server, int j, fd_set *base, int *maxfd)
{
    server[j].free = 1;
    server[j].waiting = 0;
    if (server[j].fd == *maxfd)
    {
        *maxfd = FindMaxfd(*serversNum, server);
    }
    FD_CLR(server[j].fd, base);
}
void RespondZero(struct server *server, int j, int *serversNum, int *maxfd, fd_set *base)
{
    char c = '0';
    if (bulk_write(server[j].fd, &c, 1) < 1)
    {
        if (errno == EPIPE)
        {
            connEnd(serversNum, server, j, base, maxfd);
        }
        else
            ERR("write");
    }
    server[j].offset = 0;
    server[j].waiting = 0;
    memset(server[j].line, 0, Q_LENGTH);
}
void communicate(struct server *server, int *serversNum, int *maxfd, fd_set *base)
{
    int count;
    char buff[Q_LENGTH];
    memset(&buff, 0, Q_LENGTH);
    for (int i = 0; i < *serversNum; i++)
    {
        if (server[i].free == 1)
            continue;
        if ((count = read(server[i].fd, buff, Q_LENGTH)) < 1)
        {
            if (count == 0)
            {
                connEnd(serversNum, server, i, base, maxfd);
            }
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;
            }
            else
                ERR("read");
        }
        else
        {
            strncpy(server[i].line + server[i].offset, buff, count);
            server[i].offset += count;
            if (strcmp(buff + count - 9, "Koniec") == 0)
            {
                connEnd(serversNum, server, i, base, maxfd);
                continue;
            }
            if (server[i].line[server[i].offset - 1] == 0)
            {
                if (server[i].waiting == 0)
                {
                    int j = FindWaiting(*serversNum, server);
                    if (j != -1)
                        RespondZero(server, j, serversNum, maxfd, base);
                    printf("Server %s %d : %s\n", server[i].addr, server[i].port, server[i].line);
                }
                server[i].waiting = 1;
            }
        }
    }
}
void GiveAnswer(int *serversNum, struct server *server, int *maxfd, fd_set *base)
{
    char answer[A_LENGTH];
    memset(answer, 0, A_LENGTH);
    if (read(0, answer, A_LENGTH) < 1)
        ERR("stdin");
    int i = FindWaiting(*serversNum, server);
    if (i < 0 || server[i].waiting == 0)
    {
        printf("nie teraz\n");
        return;
    }
    if (bulk_write(server[i].fd, &answer[0], 1) < 1)
    {
        if (errno == EPIPE)
        {
            connEnd(serversNum, server, i, base, maxfd);
        }
        else
            ERR("write");
    }
    server[i].offset = 0;
    server[i].waiting = 0;
    memset(server[i].line, 0, Q_LENGTH);
}
int CheckActive(struct server *server,int serversNum)
{
    int res=0;
    for(int i=0;i<serversNum;i++)
    {
        if(server[i].free==0) res++;
    }
    return res;
}
int setRfds(int serversNum, struct server *server, fd_set *base_rfds)
{
    FD_SET(0, base_rfds);
    int max = 0;
    for (int i = 0; i < serversNum; i++)
    {
        if (server[i].free == 1)
            continue;
        FD_SET(server[i].fd, base_rfds);
        if (server[i].fd > max)
            max = server[i].fd;
    }
    return max;
}
void doClient(int serversNum, struct server *server)
{
    fd_set base_rfds, rfds;
    FD_ZERO(&base_rfds);
    int maxfd = setRfds(serversNum, server, &base_rfds);
    int count;
    while (CheckActive(server,serversNum) > 0)
    {
        rfds = base_rfds;
        if ((count = select(maxfd + 1, &rfds, NULL, NULL, NULL)) > 0)
        {
            if (FD_ISSET(0, &rfds))
            {
                GiveAnswer(&serversNum, server, &maxfd, &base_rfds);
            }
            if ((count > 1 && FD_ISSET(0, &rfds)) || count > 0)
            {
                communicate(server, &serversNum, &maxfd, &base_rfds);
            }
        }
    }
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
int make_socket(void)
{
    int sock;
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        ERR("socket");
    return sock;
}
int connect_socket(char *name, char *port)
{
    struct sockaddr_in addr;
    int socketfd;
    socketfd = make_socket();
    addr = make_address(name, port);
    if (connect(socketfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0)
    {
        if (errno != EINTR)
            ERR("connect");
        else
        {
            fd_set wfds;
            int status;
            socklen_t size = sizeof(int);
            FD_ZERO(&wfds);
            FD_SET(socketfd, &wfds);
            if (TEMP_FAILURE_RETRY(select(socketfd + 1, NULL, &wfds, NULL, NULL)) < 0)
                ERR("select");
            if (getsockopt(socketfd, SOL_SOCKET, SO_ERROR, &status, &size) < 0)
                ERR("getsockopt");
            if (0 != status)
                ERR("connect");
        }
    }
    return socketfd;
}
void startServers(int serversNum, struct server *server, char **argv)
{
    int new_flags;
    int fd;
    for (int i = 0; i < serversNum; i++)
    {
        server[i].addr = argv[i * 2 + 1];
        server[i].port = atoi(argv[i * 2 + 2]);
        server[i].offset = 0;
        server[i].waiting = 0;
        memset(server[i].line, 0, Q_LENGTH);
        fd = connect_socket(argv[i * 2 + 1], argv[i * 2 + 2]);
        new_flags = fcntl(fd, F_GETFL) | O_NONBLOCK;
        fcntl(fd, F_SETFL, new_flags);
        server[i].fd = fd;
        server[i].free = 0;
    }
}
int main(int argc, char **argv)
{
    if (argc % 2 != 1 || argc < 3)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    int serversNum = (argc - 1) / 2;
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE:");
    struct server *server = (struct server *)malloc(sizeof(struct server) * serversNum);
    startServers(serversNum, server, argv);
    doClient(serversNum, server);
    for (int i = 0; i < serversNum; i++)
    {
        if (server[i].free == 1)
            continue;
        if (TEMP_FAILURE_RETRY(close(server[i].fd)) < 0)
            ERR("close");
    }
    return EXIT_SUCCESS;
}