
/**
 * C 语言中使用 char* 实现字符串的不足, 主要是因为使用“\0”表示字符串结束, 操作时需遍历字符串, 效率不高并且无法完整表示包含“\0”的数据.
 * 无法满足 Redis 保存图片等二进制数据的需求. 所以 Redis 专门设计了SDS数据结构, 在字符数组的基础上, 增加了字符数组长度 和分配空间大小等元数据.
 * 这样基于字符串长度进行的追加、复制、比较等操作, 就可以直接读取元数据, 提升效率.
 *
 * redis 在设计 SDS 结构, 定义了5种类型(就是下面5种), 它们的区别在于： 变量 len、alloc 的类型不同,
 * 比如说 sdshdr8类型的SDS, 它的变量类型为8位无符号整型. 这样设计是为了节省内存, 避免元数据比要实际数据占用的内存太大.
 */

#ifndef __SDS_H
#define __SDS_H
#define SDS_MAX_PREALLOC (1024*1024)

extern const char *SDS_NOINIT;

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>

// redis给字符数组char*取了一个别名, 也即sds, 所以在redis中的所有字符数组都是sds
typedef char *sds;

/**
 *  关于内存对齐, 有如下规则：
 *  1)所有结构体成员的字节长度都没有超出操作系统基本字节单位(32位操作系统是4, 64位操作系统是8)的情况下, 按照结构体中字节最大的变量长度来对齐;
 *  2)若结构体中某个变量字节超出操作系统基本字节单位, 那么就按照系统字节单位来对齐.
 *
 *  例如, 在X64位系统中, 基本字节单位是8字节, char是1字节, int是4字节, 均没有超过8字节, 因此会用4字节来对齐, 编辑器就会给s1分配8字节空间.
 *  struct s1 {
 *      char a;
 *      int b;
 *  };
 *
 *  如果在32位系统中, 基本字节单位是4字节, double是8字节已经超过基本单位, 那么就会用4字节来对齐, 两个char变量各用4个字节, double用2个4字节
 *  即8字节, 整个结构体编译器就会为其分配 4+8+4=16 字节.
 *  struct s1 {
 *      char name;
 *      double age;
 *      char sex;
 *  };
 *
 * 而 __attribute__ ((__packed__)) 关键字是一种编译优化, 用来实现紧凑型内存布局, 以达到节省内存的目的.
 * 加了这个关键字, 编译器就会采用紧凑型的方式分配内存, 即实际需要多少就分配多少. 也正是由于有了它, redis后续
 * 才可以在sds中通过原始的char*来定位到sds的Header, 这是获取高效处理字符串的灵魂.
 */
struct __attribute__ ((__packed__)) sdshdr5 {
    // 这个结构体已经不再使用.
    unsigned char flags;
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr8 {
    uint8_t len; /* 1字节 */
    uint8_t alloc; /* 1字节 */
    unsigned char flags; /* 1字节 */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr16 {
    uint16_t len; /* 2字节 */
    uint16_t alloc; /* 2字节 */
    unsigned char flags; /* 1字节 */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr32 {
    uint32_t len; /* 4字节 */
    uint32_t alloc; /* 4字节 */
    unsigned char flags; /* 1字节 */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr64 {
    /**
     * 字符数组当前的长度, 8字节
     */
    uint64_t len;

    /**
     * 字符数组分配的空间长度, 不包括元数据和"/0"截止字符, 8字节
     */
    uint64_t alloc;

    /**
     * SDS的类型
     */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */

    /**
     * 底层真正存储的数组
     */
    char buf[];
};

#define SDS_TYPE_5  0
#define SDS_TYPE_8  1
#define SDS_TYPE_16 2
#define SDS_TYPE_32 3
#define SDS_TYPE_64 4
#define SDS_TYPE_MASK 7
#define SDS_TYPE_BITS 3
#define SDS_HDR_VAR(T, s) struct sdshdr##T *sh = (void*)((s)-(sizeof(struct sdshdr##T)));
#define SDS_HDR(T, s) ((struct sdshdr##T *)((s)-(sizeof(struct sdshdr##T))))
#define SDS_TYPE_5_LEN(f) ((f)>>SDS_TYPE_BITS)

/**
 * 在redis中sds特指char*类型, 这里是为了获取字符串的长度.
 * 原先在C语言里, 为了获取字符串长度都是需要遍历直至遇到\0,,
 * 而redis设计了sds结构, 直接将字符串长度保存在结构体的变量中.
 * 所以呢, 这个方法首先通过char*指针拿到SDS结构体在内存的起始位置,
 * 然后再获取到len字段, 直接拿到字符串的长度.
 *
 * @param s 字符串指针
 * @return 字符串长度
 */
static inline size_t sdslen(const sds s) {
    // s本身就是一个指针, 它表示SDS结构体中的buf字段, 而它的前一个字段就是flag字段.
    // 这边通过指针运算直接拿到flag字段. 能这样子实现的前提是结构体必须是紧凑型内存结构, 如果采用了内存对齐, 这里就会拿到脏数据.
    unsigned char flags = s[-1];
    // 通过flag区分是哪一个SDS结构体类型, 再通过一次指针运算, 获取到s所在的SDS结构体
    // 在内存中的起始位置, 进而获取到len字段
    switch (flags & SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8, s)->len;
        case SDS_TYPE_16:
            return SDS_HDR(16, s)->len;
        case SDS_TYPE_32:
            return SDS_HDR(32, s)->len;
        case SDS_TYPE_64:
            return SDS_HDR(64, s)->len;
    }
    return 0;
}

static inline size_t sdsavail(const sds s) {
    unsigned char flags = s[-1];
    switch (flags & SDS_TYPE_MASK) {
        case SDS_TYPE_5: {
            return 0;
        }
        case SDS_TYPE_8: {
            SDS_HDR_VAR(8, s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_16: {
            SDS_HDR_VAR(16, s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_32: {
            SDS_HDR_VAR(32, s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_64: {
            SDS_HDR_VAR(64, s);
            return sh->alloc - sh->len;
        }
    }
    return 0;
}

static inline void sdssetlen(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch (flags & SDS_TYPE_MASK) {
        case SDS_TYPE_5: {
            unsigned char *fp = ((unsigned char *) s) - 1;
            *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
        }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8, s)->len = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16, s)->len = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32, s)->len = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64, s)->len = newlen;
            break;
    }
}

static inline void sdsinclen(sds s, size_t inc) {
    unsigned char flags = s[-1];
    switch (flags & SDS_TYPE_MASK) {
        case SDS_TYPE_5: {
            unsigned char *fp = ((unsigned char *) s) - 1;
            unsigned char newlen = SDS_TYPE_5_LEN(flags) + inc;
            *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
        }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8, s)->len += inc;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16, s)->len += inc;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32, s)->len += inc;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64, s)->len += inc;
            break;
    }
}

/* sdsalloc() = sdsavail() + sdslen() */
static inline size_t sdsalloc(const sds s) {
    unsigned char flags = s[-1];
    switch (flags & SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8, s)->alloc;
        case SDS_TYPE_16:
            return SDS_HDR(16, s)->alloc;
        case SDS_TYPE_32:
            return SDS_HDR(32, s)->alloc;
        case SDS_TYPE_64:
            return SDS_HDR(64, s)->alloc;
    }
    return 0;
}

static inline void sdssetalloc(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch (flags & SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            /* Nothing to do, this type has no total allocation info. */
            break;
        case SDS_TYPE_8:
            SDS_HDR(8, s)->alloc = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16, s)->alloc = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32, s)->alloc = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64, s)->alloc = newlen;
            break;
    }
}

sds sdsnewlen(const void *init, size_t initlen);

sds sdsnew(const char *init);

sds sdsempty(void);

sds sdsdup(const sds s);

void sdsfree(sds s);

sds sdsgrowzero(sds s, size_t len);

sds sdscatlen(sds s, const void *t, size_t len);

sds sdscat(sds s, const char *t);

sds sdscatsds(sds s, const sds t);

sds sdscpylen(sds s, const char *t, size_t len);

sds sdscpy(sds s, const char *t);

sds sdscatvprintf(sds s, const char *fmt, va_list ap);

#ifdef __GNUC__

sds sdscatprintf(sds s, const char *fmt, ...)
__attribute__((format(printf, 2, 3)));

#else
sds sdscatprintf(sds s, const char *fmt, ...);
#endif

sds sdscatfmt(sds s, char const *fmt, ...);

sds sdstrim(sds s, const char *cset);

void sdsrange(sds s, ssize_t start, ssize_t end);

void sdsupdatelen(sds s);

void sdsclear(sds s);

int sdscmp(const sds s1, const sds s2);

sds *sdssplitlen(const char *s, ssize_t len, const char *sep, int seplen, int *count);

void sdsfreesplitres(sds *tokens, int count);

void sdstolower(sds s);

void sdstoupper(sds s);

sds sdsfromlonglong(long long value);

sds sdscatrepr(sds s, const char *p, size_t len);

sds *sdssplitargs(const char *line, int *argc);

sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);

sds sdsjoin(char **argv, int argc, char *sep);

sds sdsjoinsds(sds *argv, int argc, const char *sep, size_t seplen);

/* Low level functions exposed to the user API */
sds sdsMakeRoomFor(sds s, size_t addlen);

void sdsIncrLen(sds s, ssize_t incr);

sds sdsRemoveFreeSpace(sds s);

size_t sdsAllocSize(sds s);

void *sdsAllocPtr(sds s);

/* Export the allocator used by SDS to the program using SDS.
 * Sometimes the program SDS is linked to, may use a different set of
 * allocators, but may want to allocate or free things that SDS will
 * respectively free or allocate. */
void *sds_malloc(size_t size);

void *sds_realloc(void *ptr, size_t size);

void sds_free(void *ptr);

#ifdef REDIS_TEST
int sdsTest(int argc, char *argv[]);
#endif

#endif
