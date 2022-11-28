/* Linux epoll(2) based ae.c module */

/**
 * Linux 操作系统的 IO 多路复用实现
 */

#include <sys/epoll.h>

typedef struct aeApiState {
    // epoll实例的描述符
    int epfd;
    // epoll_event结构体数组，记录监听事件
    struct epoll_event *events;
} aeApiState;

static int aeApiCreate(aeEventLoop *eventLoop) {
    aeApiState *state = zmalloc(sizeof(aeApiState));

    if (!state) return -1;
    // 将epoll_event数组保存在aeApiState结构体变量state中
    state->events = zmalloc(sizeof(struct epoll_event)*eventLoop->setsize);
    if (!state->events) {
        zfree(state);
        return -1;
    }
    // 将epoll实例描述符保存在 aeApiState 结构体变量state中
    state->epfd = epoll_create(1024); /* 1024 is just a hint for the kernel */
    if (state->epfd == -1) {
        zfree(state->events);
        zfree(state);
        return -1;
    }
    // 存储 aeApiState 结构体
    eventLoop->apidata = state;
    return 0;
}

static int aeApiResize(aeEventLoop *eventLoop, int setsize) {
    aeApiState *state = eventLoop->apidata;

    state->events = zrealloc(state->events, sizeof(struct epoll_event)*setsize);
    return 0;
}

static void aeApiFree(aeEventLoop *eventLoop) {
    aeApiState *state = eventLoop->apidata;

    close(state->epfd);
    zfree(state->events);
    zfree(state);
}

/**
 * aeCreateFileEvent函数会调用 aeApiAddEvent函数注册事件,
 * 然后 aeApiAddEvent 再通过调用 epoll_ctl 来注册希望监听的事件和相应的处理函数.
 * 一旦 aeProceeEvents 函数捕获到实际事件时, 它就会调用注册的函数对事件进行处理.
 *
 * @param eventLoop
 * @param fd
 * @param mask
 * @return
 */
static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    // 从eventLoop结构体中获取aeApiState变量, 里面保存了epoll实例
    aeApiState *state = eventLoop->apidata;
    // 创建epoll_event类型变量
    struct epoll_event ee = {0}; /* avoid valgrind warning */
    // 如果文件描述符fd对应的IO事件已存在, 则操作类型为修改, 否则为添加
    int op = eventLoop->events[fd].mask == AE_NONE ?
            EPOLL_CTL_ADD : EPOLL_CTL_MOD;

    ee.events = 0;
    mask |= eventLoop->events[fd].mask; /* Merge old events */
    // 将可读或可写IO事件类型转换为epoll监听的类型EPOLLIN(可读)或EPOLLOUT(可写)
    if (mask & AE_READABLE) ee.events |= EPOLLIN;
    if (mask & AE_WRITABLE) ee.events |= EPOLLOUT;
    //将要监听的文件描述符赋值给ee
    ee.data.fd = fd;
    // Linux 提供了 epoll_ctl API, 用于增加新的观察事件.
    if (epoll_ctl(state->epfd,op,fd,&ee) == -1) return -1;
    return 0;
}

static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int delmask) {
    aeApiState *state = eventLoop->apidata;
    struct epoll_event ee = {0}; /* avoid valgrind warning */
    int mask = eventLoop->events[fd].mask & (~delmask);

    ee.events = 0;
    if (mask & AE_READABLE) ee.events |= EPOLLIN;
    if (mask & AE_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.fd = fd;
    if (mask != AE_NONE) {
        epoll_ctl(state->epfd,EPOLL_CTL_MOD,fd,&ee);
    } else {
        /* Note, Kernel < 2.6.9 requires a non null event pointer even for
         * EPOLL_CTL_DEL. */
        epoll_ctl(state->epfd,EPOLL_CTL_DEL,fd,&ee);
    }
}

/**
 * Linux 提供了 epoll_wait API, 用于检测内核中发生的网络 IO 事件,
 * 此函数 aeApiPoll() 封装了对 epoll_wait 的调用.
 *
 * @param eventLoop
 * @param tvp
 * @return
 */
static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    aeApiState *state = eventLoop->apidata;
    int retval, numevents = 0;

    /*
     * 调用操作系统API epoll_wait() 等待内核返回监听描述符的事件产生, 该函数返回已经就绪的事件的数量. 四个参数的含义依次是:
     * 1. 为epoll_create()返回的epoll实例描述符;
     * 2. epoll_await() 要返回的已经产生的事件集合;
     * 3. 系统返回的最大事件数量;
     * 4. 超时时间
     */
    retval = epoll_wait(state->epfd, state->events, eventLoop->setsize, tvp ? (tvp->tv_sec*1000 + tvp->tv_usec/1000) : -1);
    if (retval > 0) {
        int j;

        // 监听到的事件数量
        numevents = retval;
        // 循环处理每一个事件.
        for (j = 0; j < numevents; j++) {
            int mask = 0;
            struct epoll_event *e = state->events+j;

            if (e->events & EPOLLIN) mask |= AE_READABLE;
            if (e->events & EPOLLOUT) mask |= AE_WRITABLE;
            if (e->events & EPOLLERR) mask |= AE_WRITABLE|AE_READABLE;
            if (e->events & EPOLLHUP) mask |= AE_WRITABLE|AE_READABLE;
            eventLoop->fired[j].fd = e->data.fd;
            eventLoop->fired[j].mask = mask;
        }
    }
    return numevents;
}

static char *aeApiName(void) {
    return "epoll";
}
