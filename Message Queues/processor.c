#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <ctype.h>
#include <mqueue.h>

#define MESS_SIZE 250

#define ERR(source) (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

volatile sig_atomic_t last_signal = 0;

typedef struct ArgsStruct
{
    int t;
    int p;
    char *name;
} ArgsStruct;

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s t p q2\n", name);
    fprintf(stderr, "1<=t<=10, 1<=p<=100, q2 - nazwy kolejki\n");
    exit(EXIT_FAILURE);
}

void sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}
void sig_handler(int sig)
{
    last_signal = sig;
}
void ReadArguments(ArgsStruct *a, int argc, char **argv)
{
    if (argc != 4)
        usage(argv[0]);
    a->t = atoi(argv[1]);
    a->p = atoi(argv[2]);
    if (a->t < 1 || a->t > 10 || a->p < 0 || a->p > 100)
        usage(argv[0]);
    a->name = argv[3];
}
void ChangeMessage(char *mess)
{
    char *p = strchr(mess, (int)'/') - 1;
    char *r = strchr(mess, (int)'/') + 1;
    int pid = getpid();
    while (pid != 0)
    {
        *p = pid % 10 + '0';
        pid -= pid % 10;
        pid /= 10;
        p--;
    }
    for (int i = 0; i < 3; i++)
    {
        *r = '0';
        r++;
    }
}
void WaitForMessage(mqd_t q2,char* mess,char* old_mess,int t)
{
    errno = 0;
    struct mq_attr new_attr, old_attr;
    new_attr.mq_flags = O_NONBLOCK;
    if (mq_setattr(q2, &new_attr, &old_attr) < 0)
        ERR("mq_setattr");
    while (last_signal!=SIGINT && mq_receive(q2, mess, MESS_SIZE + 1, NULL) == -1)
    {
        if (errno == EINTR || last_signal == SIGINT)
            return;
        if (errno != EAGAIN)
            ERR("mq_receive");
        if(sleep(t)!=0) break;
        printf("%s\n", old_mess);
    }
    if (mq_setattr(q2, &old_attr, NULL) < 0)
        ERR("mq_setattr");
}
void ProcessorWork(mqd_t q2, int t, int p)
{
    char mess[MESS_SIZE], old_mess[MESS_SIZE];
    srand(getpid());
    struct timespec time;
    while (last_signal != SIGINT)
    {
        memset(mess, 0, MESS_SIZE);
        clock_gettime(CLOCK_REALTIME,&time);
        time.tv_sec += 1;
        errno = 0;
        if (mq_timedreceive(q2, mess, MESS_SIZE + 1, NULL, &time) < 1)
        {
            if (errno == EINTR && last_signal == SIGINT)
                return;
            if (errno != ETIMEDOUT)
                ERR("mq_receive");
            WaitForMessage(q2,mess,old_mess,t);
        }
        if(last_signal==SIGINT) break;
        memset(old_mess, 0, MESS_SIZE);
        strcpy(old_mess, mess);
        if(sleep(t)!=0) break;
        if(last_signal==SIGINT) break;
        printf("Processor received: %s\n", mess);
        ChangeMessage(mess);
        if (rand() % 100 < p)
        {
            if (mq_send(q2, (const char *)mess, MESS_SIZE, 0))
            {
                if (errno == EINTR && last_signal == SIGINT)
                    return;
                ERR("mq_send");
            }
        }
    }
}
int main(int argc, char **argv)
{
    sethandler(sig_handler, SIGINT);
    ArgsStruct a;
    mqd_t q2;
    ReadArguments(&a, argc, argv);
    struct mq_attr attr;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MESS_SIZE + 1;
    errno = 0;
    if ((q2 = mq_open(a.name, O_RDWR, 0600, &attr)) == (mqd_t)-1)
    {
        if (errno == ENOENT)
        {
            printf("Kolejka %s nie istnieje\n", a.name);
            exit(EXIT_FAILURE);
        }
        if (errno != EINTR || last_signal != SIGINT)
            ERR("mq_open");
    }
    ProcessorWork(q2, a.t, a.p);
    mq_close(q2);
    return EXIT_SUCCESS;
}