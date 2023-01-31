/*
 * listpack也叫做紧凑列表, 设计上和 ziplist 相似, 使用一块连续的内存空间来紧凑地保存数据.
 * 一个listpack的结构如下：
 *
 * |   ←     大小为LP_HDR_SIZE    →     |     元素项1    |    元素项2      | 末尾  |
 *  ___________________________________________________________________________
 * | listpack总字节数  | listpack元素数量 | listpack entry丨listpack entry | 255  丨
 *  ---------------------------------------------------------------------------
 *
 * listpack的每个列表项不再像ziplist列表项一样, 保存前一个列表项的长度, 它只保留三个内容：
 * 1、当前元素的编码类型: entry-encoding;
 * 2、元素数据: entry-data;
 * 3、编码类型和元素数据两部分的总长度: entry-len;
 *
 * 丨entry-coding丨entry-data丨   entry-len     丨
 *  ___________________________________________
 * 丨  编码类型    丨  实际数据 丨编码类型+数据的总长度丨
 *  -------------------------------------------
 *
 */

#ifndef __LISTPACK_H
#define __LISTPACK_H

#include <stdint.h>

#define LP_INTBUF_SIZE 21 /* 20 digits of -2^63 + 1 null term = 21. */

/* lpInsert() where argument possible values: */
#define LP_BEFORE 0
#define LP_AFTER 1
#define LP_REPLACE 2

unsigned char *lpNew(void);
void lpFree(unsigned char *lp);
unsigned char *lpInsert(unsigned char *lp, unsigned char *ele, uint32_t size, unsigned char *p, int where, unsigned char **newp);
unsigned char *lpAppend(unsigned char *lp, unsigned char *ele, uint32_t size);
unsigned char *lpDelete(unsigned char *lp, unsigned char *p, unsigned char **newp);
uint32_t lpLength(unsigned char *lp);
unsigned char *lpGet(unsigned char *p, int64_t *count, unsigned char *intbuf);
unsigned char *lpFirst(unsigned char *lp);
unsigned char *lpLast(unsigned char *lp);
unsigned char *lpNext(unsigned char *lp, unsigned char *p);
unsigned char *lpPrev(unsigned char *lp, unsigned char *p);
uint32_t lpBytes(unsigned char *lp);
unsigned char *lpSeek(unsigned char *lp, long index);

#endif
