/*
 * 整数集合, 数字范围不同, intset会选择 int16/int32/int64 编码;
 * 并且 intset 在存储时是有序的, 所以可以通过二分查找来搜索元素;
 * 对于添加、更新、删除元素, 数据范围发生变化, 会引发编码长度升级或降级.
 *
 */

#ifndef __INTSET_H
#define __INTSET_H
#include <stdint.h>

typedef struct intset {
    uint32_t encoding;
    uint32_t length;
    // int8_t 类型的整数数组 contents
    int8_t contents[];
} intset;

intset *intsetNew(void);
intset *intsetAdd(intset *is, int64_t value, uint8_t *success);
intset *intsetRemove(intset *is, int64_t value, int *success);
uint8_t intsetFind(intset *is, int64_t value);
int64_t intsetRandom(intset *is);
uint8_t intsetGet(intset *is, uint32_t pos, int64_t *value);
uint32_t intsetLen(const intset *is);
size_t intsetBlobLen(intset *is);

#ifdef REDIS_TEST
int intsetTest(int argc, char *argv[]);
#endif

#endif // __INTSET_H
