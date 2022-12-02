/* Background I/O service for Redis.
 *
 * This file implements operations that we need to perform in the background.
 * Currently there is only a single operation, that is a background close(2)
 * system call. This is needed as when the process is the last owner of a
 * reference to a file closing it means unlinking it, and the deletion of the
 * file is slow, blocking the server.
 *
 * In the future we'll either continue implementing new things we need or
 * we'll switch to libeio. However there are probably long term uses for this
 * file as we may want to put here Redis specific background tasks (for instance
 * it is not impossible that we'll need a non blocking FLUSHDB/FLUSHALL
 * implementation).
 *
 * DESIGN
 * ------
 *
 * The design is trivial, we have a structure representing a job to perform
 * and a different thread and job queue for every job type.
 * Every thread waits for new jobs in its queue, and process every job
 * sequentially.
 *
 * Jobs of the same type are guaranteed to be processed from the least
 * recently inserted to the most recently inserted (older jobs processed
 * first).
 *
 * Currently there is no way for the creator of the job to be notified about
 * the completion of the operation, this will only be added when/if needed.
 *
 */

/**
 * Redis后台线程的创建和运行机制:
 *
 * 1) 先通过 bioInit 函数初始化和创建后台线程;
 * 2) 后台线程运行的是 bioProcessBackgroundJobs 函数, 该函数会轮询任务队列, 并根据要处理的任务类型, 调用相应函数进行处理;
 * 3) 后台线程要处理的任务是由 bioCreateBackgroundJob 函数来创建, 这些任务创建后会被放到任务队列中, 等待 bioProcessBackgroundJobs 函数处理.
 */

#include "server.h"
#include "bio.h"

// 保存线程描述符的数组
static pthread_t bio_threads[BIO_NUM_OPS];
// 保存互斥锁的数组
static pthread_mutex_t bio_mutex[BIO_NUM_OPS];
// 保存条件变量的两个数组
static pthread_cond_t bio_newjob_cond[BIO_NUM_OPS];
static pthread_cond_t bio_step_cond[BIO_NUM_OPS];
// 以 后台线程方式运行的任务列表
static list *bio_jobs[BIO_NUM_OPS];

/* The following array is used to hold the number of pending jobs for every
 * OP type. This allows us to export the bioPendingJobsOfType() API that is
 * useful when the main thread wants to perform some operation that may involve
 * objects shared with the background thread. The main thread will just wait
 * that there are no longer jobs of this type to be executed before performing
 * the sensible operation. This data is also useful for reporting. */
// 元素类型是 unsigned long long, 用来表示每种任务中处于等待状态的任务个数.
// 将该数组每个元素初始化为 0, 其实就是表示初始时, 每种任务都没有待处理的具体任务.
static unsigned long long bio_pending[BIO_NUM_OPS];

/* This structure represents a background Job. It is only used locally to this
 * file as the API does not expose the internals at all. */
// 用来表示后台任务
struct bio_job {
    /* 后台任务的创建时间 */
    time_t time;

    /* Job specific arguments pointers. If we need to pass more than three
     * arguments we can just pass a pointer to a structure or alike. */
    void *arg1, *arg2, *arg3;
};

void *bioProcessBackgroundJobs(void *arg);
void lazyfreeFreeObjectFromBioThread(robj *o);
void lazyfreeFreeDatabaseFromBioThread(dict *ht1, dict *ht2);
void lazyfreeFreeSlotsMapFromBioThread(zskiplist *sl);

/* Make sure we have enough stack to perform all the things we do in the
 * main thread. */
#define REDIS_THREAD_STACK_SIZE (1024*1024*4)

/**
 * 初始化后台任务作业系统, 调用 pthread_create 函数创建多个后台线程.
 */
void bioInit(void) {
    pthread_attr_t attr;
    pthread_t thread;
    size_t stacksize;
    int j;

    /*
     * 初始化互斥锁数组和条件变量数组, 然后会调用 listCreate 函数,
     * 给 bio_jobs 这个数组的每个元素创建一个列表, 同时给 bio_pending 数组的每个元素赋值为 0.
     */
    for (j = 0; j < BIO_NUM_OPS; j++) {
        pthread_mutex_init(&bio_mutex[j],NULL);
        pthread_cond_init(&bio_newjob_cond[j],NULL);
        pthread_cond_init(&bio_step_cond[j],NULL);
        bio_jobs[j] = listCreate();
        bio_pending[j] = 0;
    }

    /*
     * 调用 pthread_attr_init() 初始化线程属性变量 attr,
     * 然后调用 pthread_attr_getstacksize*() 获取线程的栈大小这一属性的当前值,
     * 并根据当前栈大小和 REDIS_THREAD_STACK_SIZE 宏定义的大小（默认值为 4MB）来计算最终的栈大小属性值.
     * 紧接着再调用 pthread_attr_setstacksize() 来设置栈大小这一属性值.
     */
    pthread_attr_init(&attr);
    pthread_attr_getstacksize(&attr,&stacksize);
    if (!stacksize) stacksize = 1; /* The world is full of Solaris Fixes */
    while (stacksize < REDIS_THREAD_STACK_SIZE) stacksize *= 2;
    pthread_attr_setstacksize(&attr, stacksize);

    /*
     * 依次为每种后台任务创建一个线程, 循环的次数是由 BIO_NUM_OPS 宏定义决定(3次)
     * 调用 3 次 pthread_create() 创建 3 个线程, 每个线程的执行函数都是: bioProcessBackgroundJobs.
     */
    for (j = 0; j < BIO_NUM_OPS; j++) {
        // 注意：在这三次线程的创建过程中, 传给 pthread_create() 的参数分别是 0、1、2.
        void *arg = (void*)(unsigned long) j;
        /*
         * pthread_create() 函数一共有4个参数, 分别是：
         * 1) *tidp, 指向线程数据结构 pthread_t 的指针;
         * 2) *attr, 指向线程属性结构 pthread_attr_t 的指针;
         * 3) *start_routine, 线程所要运行的函数的起始地址, 也是指向函数的指针;
         * 4) *arg, 传给运行函数的参数;
         */
        if (pthread_create(&thread,&attr,bioProcessBackgroundJobs,arg) != 0) {
            serverLog(LL_WARNING,"Fatal: Can't initialize Background Jobs.");
            exit(1);
        }
        bio_threads[j] = thread;
    }
}

/**
 * 创建后台任务
 *
 * @param type 任务的类型, 即：BIO_CLOSE_FILE、BIO_AOF_FSYNC、BIO_LAZY_FREE
 * @param arg1 具体任务的实际参数-1
 * @param arg2 具体任务的实际参数-2
 * @param arg3 具体任务的实际参数-3
 */
void bioCreateBackgroundJob(int type, void *arg1, void *arg2, void *arg3) {
    // 创建后台任务对应的数据结构-bio_job
    struct bio_job *job = zmalloc(sizeof(*job));
    // 设置任务的参数
    job->time = time(NULL);
    job->arg1 = arg1;
    job->arg2 = arg2;
    job->arg3 = arg3;
    pthread_mutex_lock(&bio_mutex[type]);
    // 将任务加到bio_jobs数组的对应任务列表中
    listAddNodeTail(bio_jobs[type],job);
    // 将对应任务列表上等待处理的任务个数加1
    bio_pending[type]++;
    pthread_cond_signal(&bio_newjob_cond[type]);
    pthread_mutex_unlock(&bio_mutex[type]);
}

/**
 * 该函数主要执行一个 while(1) 循环, 在循环中, 会从 bio_jobs 这个数组中取出相应任务,
 * 并根据任务类型, 调用具体的函数来执行
 *
 * @param arg 表示的就是后台任务的操作码
 * @return
 */
void *bioProcessBackgroundJobs(void *arg) {
    struct bio_job *job;
    // 这里的参数 arg 就是在调用 bioInit() 函数, for循环设置的循环次数.
    // 本质上用来做任务的类型区分.
    unsigned long type = (unsigned long) arg;
    sigset_t sigset;

    /* Check that the type is within the right interval. */
    if (type >= BIO_NUM_OPS) {
        serverLog(LL_WARNING,
            "Warning: bio thread started with wrong type %lu",type);
        return NULL;
    }

    switch (type) {
    case BIO_CLOSE_FILE:
        redis_set_thread_title("bio_close_file");
        break;
    case BIO_AOF_FSYNC:
        redis_set_thread_title("bio_aof_fsync");
        break;
    case BIO_LAZY_FREE:
        redis_set_thread_title("bio_lazy_free");
        break;
    }

    /* Make the thread killable at any time, so that bioKillThreads()
     * can work reliably. */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    pthread_mutex_lock(&bio_mutex[type]);
    /* Block SIGALRM so we are sure that only the main thread will
     * receive the watchdog signal. */
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    if (pthread_sigmask(SIG_BLOCK, &sigset, NULL))
        serverLog(LL_WARNING,
            "Warning: can't mask SIGALRM in bio.c thread: %s", strerror(errno));

    while(1) {
        listNode *ln;

        /* The loop always starts with the lock hold. */
        if (listLength(bio_jobs[type]) == 0) {
            pthread_cond_wait(&bio_newjob_cond[type],&bio_mutex[type]);
            continue;
        }
        // 从类型为type的任务队列中获取第一个任务
        ln = listFirst(bio_jobs[type]);
        job = ln->value;
        /* It is now possible to unlock the background system as we know have
         * a stand alone job structure to process.*/
        pthread_mutex_unlock(&bio_mutex[type]);

        // 判断当前处理的后台任务类型是哪一种
        if (type == BIO_CLOSE_FILE) { // 关闭文件任务
            close((long)job->arg1);
        } else if (type == BIO_AOF_FSYNC) { // AOF同步写任务
            redis_fsync((long)job->arg1);
        } else if (type == BIO_LAZY_FREE) {
            // 如果是惰性删除任务, 根据任务的参数分别调用不同的惰性删除函数执行
            /* What we free changes depending on what arguments are set:
             * arg1 -> free the object at pointer.
             * arg2 & arg3 -> free two dictionaries (a Redis DB).
             * only arg3 -> free the skiplist. */
            if (job->arg1)
                lazyfreeFreeObjectFromBioThread(job->arg1);
            else if (job->arg2 && job->arg3)
                lazyfreeFreeDatabaseFromBioThread(job->arg2,job->arg3);
            else if (job->arg3)
                lazyfreeFreeSlotsMapFromBioThread(job->arg3);
        } else {
            serverPanic("Wrong job type in bioProcessBackgroundJobs().");
        }
        zfree(job);

        /* Lock again before reiterating the loop, if there are no longer
         * jobs to process we'll block again in pthread_cond_wait(). */
        pthread_mutex_lock(&bio_mutex[type]);
        // 任务执行完成后, 调用listDelNode在任务队列中删除该任务
        listDelNode(bio_jobs[type],ln);
        // 将对应的等待任务个数减一
        bio_pending[type]--;

        /* Unblock threads blocked on bioWaitStepOfType() if any. */
        pthread_cond_broadcast(&bio_step_cond[type]);
    }
}

/* Return the number of pending jobs of the specified type. */
unsigned long long bioPendingJobsOfType(int type) {
    unsigned long long val;
    pthread_mutex_lock(&bio_mutex[type]);
    val = bio_pending[type];
    pthread_mutex_unlock(&bio_mutex[type]);
    return val;
}

/* If there are pending jobs for the specified type, the function blocks
 * and waits that the next job was processed. Otherwise the function
 * does not block and returns ASAP.
 *
 * The function returns the number of jobs still to process of the
 * requested type.
 *
 * This function is useful when from another thread, we want to wait
 * a bio.c thread to do more work in a blocking way.
 */
unsigned long long bioWaitStepOfType(int type) {
    unsigned long long val;
    pthread_mutex_lock(&bio_mutex[type]);
    val = bio_pending[type];
    if (val != 0) {
        pthread_cond_wait(&bio_step_cond[type],&bio_mutex[type]);
        val = bio_pending[type];
    }
    pthread_mutex_unlock(&bio_mutex[type]);
    return val;
}

/* Kill the running bio threads in an unclean way. This function should be
 * used only when it's critical to stop the threads for some reason.
 * Currently Redis does this only on crash (for instance on SIGSEGV) in order
 * to perform a fast memory check without other threads messing with memory. */
void bioKillThreads(void) {
    int err, j;

    for (j = 0; j < BIO_NUM_OPS; j++) {
        if (bio_threads[j] && pthread_cancel(bio_threads[j]) == 0) {
            if ((err = pthread_join(bio_threads[j],NULL)) != 0) {
                serverLog(LL_WARNING,
                    "Bio thread for job type #%d can be joined: %s",
                        j, strerror(err));
            } else {
                serverLog(LL_WARNING,
                    "Bio thread for job type #%d terminated",j);
            }
        }
    }
}
