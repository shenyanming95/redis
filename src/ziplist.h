/*
 * redis 压缩列表的定义, 本质是一块连续的内存空间, 每个元素紧凑排列, 内存利用率高.
 * 它通过使用不同的编码（满足数据长度的基础上尽可能少分配内存）来保存数据.
 * List、Hash 和 Sorted Set 三种数据类型, 都可以使用压缩列表（ziplist）来保存数据.
 *
 * 创建一个空的压缩列表, 本质就是创建一块连续的内存空间, 压缩列表的内存布局为：
 *
 * 丨    32bits   丨     32bits     丨     16bits   丨    n >= 0  丨 8 bits丨
 *  ______________________________________________________________________
 * 丨ziplist总字节数丨最后一个元素的偏移量丨ziplist元素数量丨ziplist entry丨  255  丨
 *  ----------------------------------------------------------------------
 *
 *  往ziplist插入数据时, ziplist会根据数据是字符串还是整数, 以及它们的大小进行不同的编码.
 *  这种根据数据大小进行相应编码的设计思想, 就是redis为了节省内存而设计的. 而所谓的编码, 其实
 *  指：用不同数量的字节来表示保存的信息, 体现在压缩列表元素项(ziplist entry)的结构,
 *  编码主要使用在列表元素项中的 prevlen 和 encoding 中:
 *
 * 丨 prevlen 丨   encoding  丨 data 丨
 *  _________________________________
 * 丨前一项的长度丨当前项的长度编码丨实际数据丨
 *  ---------------------------------
 *
 *  虽然ziplist通过紧凑的内存布局来保存数据, 节省内存, 但是 ziplist 也存在两个问题:
 *  1.查询复杂度高: 头尾元数据固定, 便于查找开头和结尾元素, 但对于中间元素就需要遍历列表;
 *  2.连锁更新的风险: 新元素插入位置的后续的所有元素, 都会因为前序元素的 prevlenszie 增加, 而引起自身空间也要增加;
 *
 *  @see quicklist.h
 *  @see listpack.h
 *
 */

#ifndef _ZIPLIST_H
#define _ZIPLIST_H

#define ZIPLIST_HEAD 0
#define ZIPLIST_TAIL 1

unsigned char *ziplistNew(void);
unsigned char *ziplistMerge(unsigned char **first, unsigned char **second);
unsigned char *ziplistPush(unsigned char *zl, unsigned char *s, unsigned int slen, int where);
unsigned char *ziplistIndex(unsigned char *zl, int index);
unsigned char *ziplistNext(unsigned char *zl, unsigned char *p);
unsigned char *ziplistPrev(unsigned char *zl, unsigned char *p);
unsigned int ziplistGet(unsigned char *p, unsigned char **sval, unsigned int *slen, long long *lval);
unsigned char *ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen);
unsigned char *ziplistDelete(unsigned char *zl, unsigned char **p);
unsigned char *ziplistDeleteRange(unsigned char *zl, int index, unsigned int num);
unsigned int ziplistCompare(unsigned char *p, unsigned char *s, unsigned int slen);
unsigned char *ziplistFind(unsigned char *p, unsigned char *vstr, unsigned int vlen, unsigned int skip);
unsigned int ziplistLen(unsigned char *zl);
size_t ziplistBlobLen(unsigned char *zl);
void ziplistRepr(unsigned char *zl);

#ifdef REDIS_TEST
int ziplistTest(int argc, char *argv[]);
#endif

#endif /* _ZIPLIST_H */
