#ifndef __BIO_H
#define __BIO_H

/**
 * Redis后台线程的创建和运行机制:
 *
 * 1) 先通过 bioInit 函数初始化和创建后台线程;
 * 2) 后台线程运行的是 bioProcessBackgroundJobs 函数, 该函数会轮询任务队列, 并根据要处理的任务类型, 调用相应函数进行处理;
 * 3) 后台线程要处理的任务是由 bioCreateBackgroundJob 函数来创建, 这些任务创建后会被放到任务队列中, 等待 bioProcessBackgroundJobs 函数处理.
 */

/* Exported API */
void bioInit(void);
void bioCreateBackgroundJob(int type, void *arg1, void *arg2, void *arg3);
unsigned long long bioPendingJobsOfType(int type);
unsigned long long bioWaitStepOfType(int type);
time_t bioOlderJobOfType(int type);
void bioKillThreads(void);

/* Background job opcodes */
#define BIO_CLOSE_FILE    0 /* 文件关闭后台任务 */
#define BIO_AOF_FSYNC     1 /* AOF 日志同步写回后台任务 */
#define BIO_LAZY_FREE     2 /* 惰性删除后台任务 */
#define BIO_NUM_OPS       3 /* 宏定义为3, 其实就是对应上面3种类型的任务 */

#endif
