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
 * 丨entry-coding丨 entry-data 丨   entry-len     丨
 *  ______________________________________________
 * 丨  编码类型    丨  实际数据   丨编码类型+数据的总长度丨
 *  ----------------------------------------------
 *  其中 entry-len 的编码方式具有特殊化的设计, entry-len 每个字节的最高位用来表示当前字节是否为 entry-len 的最后一个字节, 有两种情况:
 *  1.最高位为 1, 表示 entry-len 还没有结束, 当前字节的左边字节仍然表示 entry-len 的内容;
 *  2.最高位为 0, 表示当前字节已经是 entry-len 最后一个字节;
 *  entry-len 每个字节的低 7 位, 则记录了实际的长度信息, 同时采用了大端模式存储, 即entry-len 的低位字节保存在内存高地址.
 *
 *
 *  与 ziplist 相比, listpack 内的元素不会再记录 prev 元素的长度, 因此在新增/删除元素, 就只涉及到当前列表项.
 *  listpack 能支持正、反向查询:
 *  1) 从左到右正向查询: 指针向右偏移 LP_HDR_SIZE 大小, 跳过 listpack 头结构, 调用 lpCurrentEncodedSize 函数计算
 *                    编码类型和实际数据总长度, 调用 lpEncodeBacklen 函数计算 entry-len 长度. 指针继续向右偏移得到下一个元素.
 *  2) 从右到左反向查询: listpack 头部的 listpack 总长度, 指针偏移到 listpack 的尾部, 调用 lpDecodeBacklen 函数逐个读取当前
 *                    列表项的 entry-len, 让指针向左偏移得到下一个元素.
 *
 *  @see quicklist.h
 *  @see ziplist.h
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
