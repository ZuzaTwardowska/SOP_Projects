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
    int n;
    char *name1;
    char *name2;
} ArgsStruct;

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s t p q1 q2 (n)\n", name);
    fprintf(stderr, "1<=t<=10, 1<=p<=100, q1,q2 - nazwy kolejek, (1<=n<=10)\n");
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
    if (argc != 5 && argc != 6)
        usage(argv[0]);
    a->t = atoi(argv[1]);
    a->p = atoi(argv[2]);
    if (argc == 6)
    {
        a->n = atoi(argv[5]);
        if (a->n < 1 || a->n > 10)
            usage(argv[0]);
    }
    else
        a->n = -1;
    if (a->t < 1 || a->t > 10 || a->p < 0 || a->p > 100)
        usage(argv[0]);
    a->name1 = argv[3];
    a->name2 = argv[4];
}
void CheckIfQExists(char *name1, char *name2)
{
    mqd_t q2, q1;
    errno = 0;
    if ((q1 = mq_open(name1, O_RDWR, 0600, 0)) == (mqd_t)-1)
    {
        if (errno == EINTR || last_signal == SIGINT)
            return;
        if (errno == ENOENT)
        {
            printf("Kolejka %s nie istnieje\n", name1);
            exit(EXIT_FAILURE);
        }
        ERR("mq_open");
    }
    else
        mq_close(q1);
    errno = 0;
    if ((q2 = mq_open(name2, O_RDWR, 0600, 0)) == (mqd_t)-1)
    {
        if (errno == EINTR || last_signal == SIGINT)
            return;
        if (errno == ENOENT)
        {
            printf("Kolejka %s nie istnieje\n", name2);
            exit(EXIT_FAILURE);
        }
        ERR("mq_open");
    }
    else
        mq_close(q2);
}
void CreateMessage(char *mess)
{
    int i = 0;
    int pid = getpid();
    while (pid != 0)
    {
        mess[i] = pid % 10 + '0';
        pid -= pid % 10;
        pid /= 10;
        i++;
    }
    mess[i++] = '/';
    for (int j = 0; j < 3; j++)
    {
        mess[i + j] = 'a' + rand() % ('z' - 'a');
    }
}
void SendMessagesToQ1(mqd_t q1, int n)
{
    char mess[MESS_SIZE];
    for (int i = 0; i < n; i++)
    {
        if (last_signal == SIGINT)
            break;
        memset(mess, 0, MESS_SIZE);
        CreateMessage(mess);
        if (mq_send(q1, (const char *)mess, MESS_SIZE, 0))
        {
            if (errno == EINTR || last_signal == SIGINT)
                return;
            if (errno == EAGAIN)
            {
                printf("Mqueue full\n");
                break;
            }
            else
                ERR("mq_send");
        }
    }
}
void ChangeMessage(char *new_mess)
{
    char *p = strchr(new_mess, 0);
    *p = '/';
    p++;
    for (int i = 0; i < 5; i++)
    {
        *p = 'a' + rand() % ('z' - 'a');
        p++;
    }
}
void GeneratorWork(mqd_t q1, mqd_t q2, int t, int p)
{
    char new_mess[MESS_SIZE], received_mess[MESS_SIZE];
    while (last_signal != SIGINT)
    {
        memset(received_mess, 0, MESS_SIZE);
        memset(new_mess, 0, MESS_SIZE);
        if (mq_receive(q1, received_mess, MESS_SIZE + 1, NULL) < 1)
        {
            if (errno == EINTR || last_signal == SIGINT)
                return;
            ERR("mq_receive");
        }
        if (sleep(t) != 0)
            break;
        if (rand() % 100 < p)
        {
            strcpy(new_mess, received_mess);
            ChangeMessage(new_mess);
            if (mq_send(q2, (const char *)new_mess, MESS_SIZE, 1))
            {
                if (errno == EINTR || last_signal == SIGINT)
                    return;
                ERR("mq_send");
            }
        }
        if (mq_send(q1, (const char *)received_mess, MESS_SIZE, 0))
        {
            if (errno == EINTR || last_signal == SIGINT)
                return;
            ERR("mq_send");
        }
    }
}
int main(int argc, char **argv)
{
    sethandler(sig_handler, SIGINT);
    ArgsStruct a;
    ReadArguments(&a, argc, argv);
    mqd_t q2, q1;
    if (a.n == -1)
        CheckIfQExists(a.name1, a.name2);
    else
    {
        errno = 0;
        if (mq_unlink(a.name1))
            if (errno != ENOENT)
                ERR("mq_unlink");
        errno = 0;
        if (mq_unlink(a.name2))
            if (errno != ENOENT)
                ERR("mq_unlink");
    }
    struct mq_attr attr;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MESS_SIZE + 1;

    if ((q1 = mq_open(a.name1, O_RDWR | O_NONBLOCK | O_CREAT, 0600, &attr)) == (mqd_t)-1)
        if (errno != EINTR && last_signal != SIGINT)
            ERR("mq_open");
    if ((q2 = mq_open(a.name2, O_WRONLY | O_CREAT, 0600, &attr)) == (mqd_t)-1)
        if (errno != EINTR && last_signal != SIGINT)
            ERR("mq_open");
    srand(getpid());
    if (a.n != -1)
        SendMessagesToQ1(q1, a.n);
    GeneratorWork(q1, q2, a.t, a.p);
    mq_close(q2);
    mq_close(q1);

    return EXIT_SUCCESS;
}