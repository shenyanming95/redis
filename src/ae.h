/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 */

/**
 * Redis的网络框架实现了Reactor模型, 并以此开发事件驱动框架.
 * 该框架的代码实现是 ae.c, 对应的头文件是 ae.h
 */

#ifndef __AE_H__
#define __AE_H__

#include <time.h>

#define AE_OK 0
#define AE_ERR -1

#define AE_NONE 0       /* No events registered. */
#define AE_READABLE 1   /* Fire when descriptor is readable. */
#define AE_WRITABLE 2   /* Fire when descriptor is writable. */
#define AE_BARRIER 4    /* With WRITABLE, never fire the event if the
                           READABLE event already fired in the same event
                           loop iteration. Useful when you want to persist
                           things to disk before sending replies, and want
                           to do that in a group fashion. */

#define AE_FILE_EVENTS 1
#define AE_TIME_EVENTS 2
#define AE_ALL_EVENTS (AE_FILE_EVENTS|AE_TIME_EVENTS)
#define AE_DONT_WAIT 4
#define AE_CALL_AFTER_SLEEP 8

#define AE_NOMORE -1
#define AE_DELETED_EVENT_ID -1

/* Macros */
#define AE_NOTUSED(V) ((void) V)

struct aeEventLoop;

/* Types and data structures */
typedef void aeFileProc(struct aeEventLoop *eventLoop, int fd, void *clientData, int mask);

typedef int aeTimeProc(struct aeEventLoop *eventLoop, long long id, void *clientData);

typedef void aeEventFinalizerProc(struct aeEventLoop *eventLoop, void *clientData);

typedef void aeBeforeSleepProc(struct aeEventLoop *eventLoop);

/**
 * Redis的事件驱动框架, 只定义了两类事件：
 * 1. IO事件, 即{@link aeFileEvent}, 对应客户端发送的网络请求. 包括三类：可读、可写和屏障事件.
 * 2. 时间事件, 即{@Link aeTimeEvent}, 对应Redis自身的周期性操作.
 */

/* I/O 事件结构体 */
typedef struct aeFileEvent {
    /**
     * 用来表示事件类型的掩码, 主要有 AE_READABLE、AE_WRITABLE 和 AE_BARRIER 三种类型事件. 初始化为 AE_NONE, 表示没有任何事件. 其中：
     * 1.AE_READABLE：可读事件, 从客户端读取数据;
     * 2.AE_WRITABLE：可写事件, 向客户端写入数据;
     * 3.AE_BARRIER：屏障事件, 反转事件的处理顺序. 比如一般redis优先给客户端返回数据, 但如果需要将server端数据写入磁盘, 就会使用屏障事件, 让写入优先于客户端返回事件执行.
     */
    int mask;

    /**
     * 这两个属性, 分别指向AE_READABLE 和 AE_WRITABLE 这两类事件的处理函数.
     * 即 Reactor 模型中的 handle 角色.
     */
    aeFileProc *rfileProc;
    aeFileProc *wfileProc;

    /**
     * 指向客户端私有数据的指针
     */
    void *clientData;
} aeFileEvent;

/* 时间事件结构体 */
typedef struct aeTimeEvent {
    /**
     * 时间事件ID
     */
    long long id;

    /**
     * 事件到达的秒级时间戳
     */
    long when_sec;

    /**
     * 事件到达的毫秒级时间戳
     */
    long when_ms;

    /**
     * 时间事件触发后的处理函数
     */
    aeTimeProc *timeProc;

    /**
     * 事件结束后的处理函数
     */
    aeEventFinalizerProc *finalizerProc;

    /**
     * 事件相关的私有数据
     */
    void *clientData;

    /**
     * 时间事件链表的前向指针
     */
    struct aeTimeEvent *prev;

    /**
     * 时间事件链表的后向指针
     */
    struct aeTimeEvent *next;
} aeTimeEvent;

/* A fired event */
typedef struct aeFiredEvent {
    int fd;
    int mask;
} aeFiredEvent;

/**
 * 事件驱动框架循环流程的数据结构.
 * 会在 Redis server 在完成初始化后创建, 执行逻辑在：在server.c的 initServer 函数中,
 * 通过调用 aeCreateEventLoop 函数进行初始化.
 */
typedef struct aeEventLoop {
    int maxfd;   /* highest file descriptor currently registered */
    int setsize; /* max number of file descriptors tracked */
    long long timeEventNextId;
    time_t lastTime;     /* Used to detect system clock skew */

    aeFileEvent *events;        // 客户端 I/O 事件数组
    aeFiredEvent *fired;        // 已触发事件数组
    aeTimeEvent *timeEventHead; // 按一定时间周期触发的事件的链表头

    int stop; // 停止循环事件的标记符
    void *apidata;  // 用于存储底层操作系统实现I/O多路复用的os-api变量, 实际为各个实现源文件的aeApiState结构体.
    aeBeforeSleepProc *beforesleep; // 进入事件循环流程前执行的函数, 即 server.c 中的 beforeSleep() 函数.
    aeBeforeSleepProc *aftersleep;  // 退出事件循环流程后执行的函数, 即 server.c 中的 afterSleep() 函数.
    int flags;
} aeEventLoop;

/* Prototypes */
aeEventLoop *aeCreateEventLoop(int setsize);

void aeDeleteEventLoop(aeEventLoop *eventLoop);

void aeStop(aeEventLoop *eventLoop);

/**
 * 负责事件和 handler 注册的函数
 *
 * @param eventLoop 事件循环流程结构体
 * @param fd IO 事件对应的文件描述符 fd
 * @param mask 事件类型掩码 mask
 * @param proc 事件处理回调函数*proc
 * @param clientData 事件私有数据*clientData
 * @return
 */
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask, aeFileProc *proc, void *clientData);

void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask);

int aeGetFileEvents(aeEventLoop *eventLoop, int fd);

/**
 * 创建时间事件: aeTimeEvent
 *
 * @param eventLoop 事件循环驱动变量
 * @param milliseconds 要创建的这个时间事件的触发时间距离当前时间的时长, 用毫秒表示
 * @param proc 要创建的时间事件触发后的回调函数: serverCron(server.c文件)
 * @param clientData
 * @param finalizerProc
 * @return
 */
long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
                            aeTimeProc *proc, void *clientData,
                            aeEventFinalizerProc *finalizerProc);

int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id);

/**
 * 负责事件捕获与分发的函数
 */
int aeProcessEvents(aeEventLoop *eventLoop, int flags);

int aeWait(int fd, int mask, long long milliseconds);

/**
 * Redis事件驱动框架的入口函数
 * @param eventLoop
 */
void aeMain(aeEventLoop *eventLoop);

char *aeGetApiName(void);

void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep);

void aeSetAfterSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *aftersleep);

int aeGetSetSize(aeEventLoop *eventLoop);

int aeResizeSetSize(aeEventLoop *eventLoop, int setsize);

void aeSetDontWait(aeEventLoop *eventLoop, int noWait);

#endif
