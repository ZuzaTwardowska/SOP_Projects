#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <signal.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#define QUANTITY 10
#define LENGTH 64
#define SIZE QUANTITY *LENGTH * sizeof(char)
#define BACKLOG 3
#define ERR(source) (perror(source),                                 \
                     fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), \
                     exit(EXIT_FAILURE))
typedef struct
{
    char tab[QUANTITY][LENGTH];
} arr;
typedef struct
{
    int free;
    int num;
    int *clientNumber;
    int fd;
    arr *map;
    sem_t *sem;
    sem_t *main_sem;
} targ;

volatile sig_atomic_t do_work = 1;

void siginthandler(int sig)
{
    do_work = 0;
}
void usage(char *name)
{
    fprintf(stderr, "USAGE: %s -option(p or k) ...\nproducent: -p port\n", name);
    exit(EXIT_FAILURE);
}
void sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0x00, sizeof(struct sigaction));
    act.sa_handler = f;

    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}
int make_socket(int domain, int type)
{
    int sock;
    sock = socket(domain, type, 0);
    if (sock < 0)
        ERR("socket");
    return sock;
}
int bind_tcp_socket(uint16_t port)
{
    struct sockaddr_in addr;
    int socketfd, t = 1;
    socketfd = make_socket(PF_INET, SOCK_STREAM);
    memset(&addr, 0x00, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
        ERR("setsockopt");
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR("bind");
    if (listen(socketfd, BACKLOG) < 0)
        ERR("listen");
    return socketfd;
}
int add_new_client(int sfd)
{
    int nfd;
    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(addr);
    if ((nfd = TEMP_FAILURE_RETRY(accept(sfd, &addr, &addr_size))) < 0)
    {
        if (EAGAIN == errno || EWOULDBLOCK == errno)
            return -1;
        ERR("accept");
    }
    return nfd;
}
void setMaskAndHandlers(sigset_t *mask, sigset_t *oldmask)
{
    sethandler(SIG_IGN, SIGPIPE);
    sethandler(siginthandler, SIGINT);
    sigemptyset(mask);
    sigaddset(mask, SIGINT);
    sigprocmask(SIG_BLOCK, mask, oldmask);
}
int createSocketDescriptor(int port)
{
    int fd = bind_tcp_socket(port);
    int new_flags = fcntl(fd, F_GETFL) | O_NONBLOCK;
    if (fcntl(fd, F_SETFL, new_flags) == -1)
        ERR("fcntl");
    return fd;
}
sem_t **createSemaphores()
{
    sem_t **sems = (sem_t **)malloc(sizeof(sem_t *) * QUANTITY);
    if(!sems) ERR("malloc");
    char name[5];
    for (int i = 0; i < QUANTITY; i++)
    {
        memset(&name, 0, 5);
        snprintf(name, 5, "sem%d", i);
        if ((sems[i] = sem_open(name, O_CREAT, 0666, 0)) == SEM_FAILED)
            ERR("sem_open");
    }
    return sems;
}
targ *init(int *clientCount, arr *map)
{
    sem_t *main_sem;
    if ((main_sem = sem_open("con_semaphore", O_CREAT, 0666, 0)) == SEM_FAILED)
        ERR("sem_open");
    targ *args = (targ *)malloc(QUANTITY * sizeof(targ));
    if(!args) ERR("malloc");
    for (int i = 0; i < QUANTITY; i++)
    {
        args[i].clientNumber = clientCount;
        args[i].free = 1;
        args[i].num = i;
        args[i].map = map;
        args[i].main_sem = main_sem;
    }
    return args;
}
void thread_clean_up(void* ptr)
{
    targ* arg=(targ*)ptr;
    (*arg->clientNumber)--;
    if (close(arg->fd) < 0)
        ERR("close");
    arg->free = 1;
}
void *serveClient(void *ptr)
{
    targ *arg = (targ *)ptr;
    char buf[LENGTH];
    pthread_cleanup_push(thread_clean_up,ptr);
    while (do_work)
    {
        sleep(1);
        memset(&buf, 0, LENGTH);
        memset(arg->map->tab[arg->num], 0,LENGTH);
        if ((TEMP_FAILURE_RETRY(read(arg->fd, &buf, LENGTH))) < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno == ECONNRESET || errno==EPIPE)
                break;
            ERR("read");
        }
        snprintf(arg->map->tab[arg->num], LENGTH, "%s", buf);
        if (sem_post(arg->main_sem) < 0)
            ERR("sem_post");
        if(sem_wait(arg->sem)<0)
            ERR("sem_wait");
        if(TEMP_FAILURE_RETRY(write(arg->fd,(char*)arg->map->tab[arg->num],LENGTH))<0)
        {
            if (errno == EINTR)
                continue;
            if (errno == ECONNRESET || errno==EPIPE)
                break;
            ERR("write");
        }
        memset((char*)arg->map->tab[arg->num],0,LENGTH);
    }
    pthread_cleanup_pop(1);
    return NULL;
}
int findFreeIndex(targ *args)
{
    for (int i = 0; i < QUANTITY; i++)
    {
        if (args[i].free)
            return i;
    }
    return -1;
}
arr *connectToMap()
{
    int mapfd;
    if ((mapfd = shm_open("my_memory", O_CREAT | O_RDWR, 0666)) == -1)
        ERR("shm_open");
    if (ftruncate(mapfd, SIZE) < 0)
        ERR("ftruncate");
    arr *map;
    if ((map = (arr *)mmap(0, SIZE, PROT_WRITE, MAP_SHARED, mapfd, 0)) == MAP_FAILED)
        ERR("mmap");
    return map;
}
void closeSemaphores(sem_t** sems,sem_t* main_sem)
{
    for(int i=0;i<QUANTITY;i++)
    {
        if(sem_close(sems[i])<0) ERR("close");
    }
    free(sems);
    if(sem_close(main_sem)<0) ERR("close");
}
void closeFds(targ* args)
{
    for (int i = 0; i < QUANTITY; i++)
    {
        if (!args[i].free)
        {
            if (close(args[i].fd) < 0)
                ERR("close");
        }
    }
}
void cancelThreads(pthread_t* threads,targ* args)
{
    for(int i=0;i<QUANTITY;i++)
    {
        if(args[i].free) continue;
        if(pthread_cancel(threads[i])<0) ERR("pthread_cancel");
        if(pthread_join(threads[i],NULL)<0) ERR("pthread_join");
    }
}
void producent(int port)
{
    sigset_t mask, oldmask;
    setMaskAndHandlers(&mask, &oldmask);
    int fd = createSocketDescriptor(port);
    int clientfd, clientCount = 0,f;
    fd_set base_rfds, rfds;
    FD_ZERO(&base_rfds);
    FD_SET(fd, &base_rfds);
    pthread_t threads[QUANTITY];
    arr *map = connectToMap();
    sem_t **sems = createSemaphores();
    targ *args = init(&clientCount, map);
    while (do_work)
    {
        rfds = base_rfds;
        if (pselect(fd + 1, &rfds, NULL, NULL, NULL, &oldmask) > 0)
        {
            if ((clientfd = add_new_client(fd)) == -1)
                continue;
            if (clientCount >= QUANTITY)
            {
                if (close(clientfd) < 0)
                    ERR("close");
                continue;
            }
            if((f = findFreeIndex(args))<0) continue;
            args[f].fd = clientfd;
            args[f].free = 0;
            args[f].sem = sems[f];
            if (pthread_create(&threads[f], NULL, serveClient, (void *)&args[f]) != 0)
                ERR("pthread_create");
            clientCount++;
        }
        else
        {
            if (errno == EINTR)
                continue;
            ERR("pselect");
        }
    }
    cancelThreads(threads, args);
    closeFds(args);
    closeSemaphores(sems,args[0].main_sem);
    free(args);
    if (TEMP_FAILURE_RETRY(close(fd)) < 0)
        ERR("close");
    if (shm_unlink("my_memory") < 0)
    {
        if (errno == ENOENT)
            return;
        ERR("shm_unlink");
    }
}
void ReverseAndSend(char* buf,char* map)
{   
    int a=0;
    char temp[LENGTH];
    memset(&temp,0,LENGTH);
    for(int i=LENGTH-1;i>=0;i--)
    {
        if(buf[i]==0) continue;
        temp[a++]=buf[i];
    }
    sleep(1);
    snprintf(map, LENGTH, "%s", temp);
}
void consument()
{
    sethandler(siginthandler, SIGINT);
    sem_t **sems = createSemaphores();
    sem_t *con_sem,*mutual_sem;
    if ((con_sem = sem_open("con_semaphore", O_CREAT, 0666, 0)) == SEM_FAILED)
        ERR("sem_open");
    if((mutual_sem=sem_open("mutual_semaphore", O_CREAT, 0666, 1)) == SEM_FAILED)
        ERR("sem_open");
    arr *map = connectToMap();
    char buf[LENGTH];
    while (do_work)
    {
        errno=0;
        if(sem_wait(con_sem)<0)
        {
            if(errno==EINTR) continue;
            ERR("sem_wait");
        }    
        if(sem_wait(mutual_sem)<0)
        {
            if(errno==EINTR) continue;
            ERR("sem_wait");
        } 
        for (int i = 0; i < QUANTITY; i++)
        {
            if(map->tab[i][0]==0) continue;
            memset(&buf,0,LENGTH);
            snprintf(buf,LENGTH,"%s", (char *)map->tab[i]);
            memset((char *)map->tab[i],0,LENGTH);
            ReverseAndSend(buf,map->tab[i]);
            if(sem_post(sems[i])<0)
                ERR("sem_post");
            break;
        }
        if(sem_post(mutual_sem)<0)
                ERR("sem_post");
    }
    closeSemaphores(sems,con_sem);
    if (shm_unlink("my_memory") < 0)
    {
        if (errno == ENOENT)
            return;
        ERR("shm_unlink");
    }
    if(sem_close(mutual_sem)<0) ERR("close");
}
int main(int argc, char **argv)
{
    if (argc < 2)
        usage(argv[0]);
    if (strcmp(argv[1], "-p") == 0)
    {
        if (argc != 3)
            usage(argv[0]);
        producent(atoi(argv[2]));
    }
    else
    {
        if (argc != 2)
            usage(argv[0]);
        consument();
    }
    return EXIT_SUCCESS;
}