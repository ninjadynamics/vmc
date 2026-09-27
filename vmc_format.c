/* Creates a formatted 8 MB PS2 memory card image with ECC (PCSX2's Mcd00N.ps2
 * layout), the same structures as HyperSolar's tools/vmc/ps2mc.py and ps2sdk
 * mcman's McFormat on a card with no bad blocks: superblock in cluster 0, the
 * indirect FAT in cluster 8, the FAT in clusters 9-40, allocatable clusters
 * from 41 with the root directory ("." and "..") first, backup blocks 1022
 * and 1023 erased. Erased flash reads as 0xFF (data and spare). The root
 * entries carry a zero timestamp (the IOP has no clock here); byte for byte
 * otherwise the same image as `ps2mc.py format`.
 */
#include "irx_imports.h"
#include "vmc_format.h"

#define PAGE            512
#define RAW_PAGE        (PAGE + 16)
#define PAGES_PER_CLUSTER 2
#define CLUSTER         (PAGE * PAGES_PER_CLUSTER)
#define PAGES_PER_BLOCK 16
#define CLUSTERS_PER_BLOCK (PAGES_PER_BLOCK / PAGES_PER_CLUSTER)
#define CLUSTERS_PER_CARD 8192
#define TOTAL_PAGES     (CLUSTERS_PER_CARD * PAGES_PER_CLUSTER)
#define FAT_PER_CLUSTER (CLUSTER / 4)
#define IFC_CLUSTER     CLUSTERS_PER_BLOCK                               /* 8 */
#define FAT_CLUSTERS    ((CLUSTERS_PER_CARD * 4 + CLUSTER - 1) / CLUSTER) /* 32 */
#define FIRST_FAT       (IFC_CLUSTER + 1)                                /* 9 */
#define ALLOC_OFFSET    (FIRST_FAT + FAT_CLUSTERS)                       /* 41 */
#define BACKUP1         (CLUSTERS_PER_CARD / CLUSTERS_PER_BLOCK - 1)     /* 1023 */
#define BACKUP2         (BACKUP1 - 1)                                    /* 1022 */
#define ALLOC_END       (BACKUP2 * CLUSTERS_PER_BLOCK - ALLOC_OFFSET)    /* 8135 */
#define MAX_ALLOC       8000                                             /* mcman's rounding */
#define CHUNK_PAGES     32                                               /* per host write */

static const unsigned char ecc_table[256] = {
    0x00, 0x87, 0x96, 0x11, 0xa5, 0x22, 0x33, 0xb4, 0xb4, 0x33, 0x22, 0xa5, 0x11, 0x96, 0x87, 0x00,
    0xc3, 0x44, 0x55, 0xd2, 0x66, 0xe1, 0xf0, 0x77, 0x77, 0xf0, 0xe1, 0x66, 0xd2, 0x55, 0x44, 0xc3,
    0xd2, 0x55, 0x44, 0xc3, 0x77, 0xf0, 0xe1, 0x66, 0x66, 0xe1, 0xf0, 0x77, 0xc3, 0x44, 0x55, 0xd2,
    0x11, 0x96, 0x87, 0x00, 0xb4, 0x33, 0x22, 0xa5, 0xa5, 0x22, 0x33, 0xb4, 0x00, 0x87, 0x96, 0x11,
    0xe1, 0x66, 0x77, 0xf0, 0x44, 0xc3, 0xd2, 0x55, 0x55, 0xd2, 0xc3, 0x44, 0xf0, 0x77, 0x66, 0xe1,
    0x22, 0xa5, 0xb4, 0x33, 0x87, 0x00, 0x11, 0x96, 0x96, 0x11, 0x00, 0x87, 0x33, 0xb4, 0xa5, 0x22,
    0x33, 0xb4, 0xa5, 0x22, 0x96, 0x11, 0x00, 0x87, 0x87, 0x00, 0x11, 0x96, 0x22, 0xa5, 0xb4, 0x33,
    0xf0, 0x77, 0x66, 0xe1, 0x55, 0xd2, 0xc3, 0x44, 0x44, 0xc3, 0xd2, 0x55, 0xe1, 0x66, 0x77, 0xf0,
    0xf0, 0x77, 0x66, 0xe1, 0x55, 0xd2, 0xc3, 0x44, 0x44, 0xc3, 0xd2, 0x55, 0xe1, 0x66, 0x77, 0xf0,
    0x33, 0xb4, 0xa5, 0x22, 0x96, 0x11, 0x00, 0x87, 0x87, 0x00, 0x11, 0x96, 0x22, 0xa5, 0xb4, 0x33,
    0x22, 0xa5, 0xb4, 0x33, 0x87, 0x00, 0x11, 0x96, 0x96, 0x11, 0x00, 0x87, 0x33, 0xb4, 0xa5, 0x22,
    0xe1, 0x66, 0x77, 0xf0, 0x44, 0xc3, 0xd2, 0x55, 0x55, 0xd2, 0xc3, 0x44, 0xf0, 0x77, 0x66, 0xe1,
    0x11, 0x96, 0x87, 0x00, 0xb4, 0x33, 0x22, 0xa5, 0xa5, 0x22, 0x33, 0xb4, 0x00, 0x87, 0x96, 0x11,
    0xd2, 0x55, 0x44, 0xc3, 0x77, 0xf0, 0xe1, 0x66, 0x66, 0xe1, 0xf0, 0x77, 0xc3, 0x44, 0x55, 0xd2,
    0xc3, 0x44, 0x55, 0xd2, 0x66, 0xe1, 0xf0, 0x77, 0x77, 0xf0, 0xe1, 0x66, 0xd2, 0x55, 0x44, 0xc3,
    0x00, 0x87, 0x96, 0x11, 0xa5, 0x22, 0x33, 0xb4, 0xb4, 0x33, 0x22, 0xa5, 0x11, 0x96, 0x87, 0x00,
};

static unsigned char g_chunk[CHUNK_PAGES * RAW_PAGE];

static void put32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void put16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

/* 3 ECC bytes per 128 data bytes (PCSX2 FolderMemoryCard::CalculateECC). */
static void ecc128(unsigned char *ecc, const unsigned char *data)
{
    unsigned char e0 = 0, e1 = 0, e2 = 0;
    int i;

    for (i = 0; i < 128; i++) {
        const unsigned char c = ecc_table[data[i]];
        e0 ^= c;
        if (c & 0x80) {
            e1 ^= (unsigned char)~i;
            e2 ^= (unsigned char)i;
        }
    }
    ecc[0] = (unsigned char)(~e0 & 0x77);
    ecc[1] = (unsigned char)(~e1 & 0x7f);
    ecc[2] = (unsigned char)(~e2 & 0x7f);
}

/* One directory entry (McFsEntry, 512 bytes) with a zero timestamp. */
static void fsentry(unsigned char *e, unsigned int mode, unsigned int length, const char *name)
{
    memset(e, 0, PAGE);
    put16(e, mode);
    put32(e + 4, length);
    strcpy((char *)e + 64, name);
}

/* Fills one page's 512 data bytes; returns 0 when the page stays erased. */
static int page_data(int page, unsigned char *d)
{
    const int cluster = page / PAGES_PER_CLUSTER;
    const int half = page % PAGES_PER_CLUSTER;
    int i;

    memset(d, 0xff, PAGE);
    if (cluster == 0 && half == 0) {                  /* superblock */
        memset(d, 0, 0x180);
        strcpy((char *)d, "Sony PS2 Memory Card Format ");
        strcpy((char *)d + 28, "1.2.0.0");
        put16(d + 0x28, PAGE);
        put16(d + 0x2a, PAGES_PER_CLUSTER);
        put16(d + 0x2c, PAGES_PER_BLOCK);
        put16(d + 0x2e, 0xff00);
        put32(d + 0x30, CLUSTERS_PER_CARD);
        put32(d + 0x34, ALLOC_OFFSET);
        put32(d + 0x38, ALLOC_END);
        put32(d + 0x3c, 0);                           /* root directory */
        put32(d + 0x40, BACKUP1);
        put32(d + 0x44, BACKUP2);
        put32(d + 0x50, IFC_CLUSTER);                 /* ifc_list[0]; the rest 0 */
        for (i = 0; i < 32; i++)
            put32(d + 0xd0 + 4 * i, 0xffffffff);      /* bad_block_list: none */
        d[0x150] = 2;                                 /* PS2 card */
        d[0x151] = 0x52;                              /* card flags */
        put32(d + 0x154, CLUSTER);                    /* mcman's in-memory tail */
        put32(d + 0x158, FAT_PER_CLUSTER);
        put32(d + 0x15c, CLUSTERS_PER_BLOCK);
        put32(d + 0x160, 0xffffffff);
        put32(d + 0x170, MAX_ALLOC);
        put32(d + 0x17c, 0xffffffff);
        return 1;
    }
    if (cluster < CLUSTERS_PER_BLOCK)                 /* rest of block 0: blank, with ECC */
        return 1;
    if (cluster == IFC_CLUSTER) {                     /* indirect FAT: the FAT clusters */
        if (half == 0)
            for (i = 0; i < FAT_CLUSTERS; i++)
                put32(d + 4 * i, FIRST_FAT + i);
        return 1;
    }
    if (cluster >= FIRST_FAT && cluster < ALLOC_OFFSET) {   /* FAT */
        const int first = (cluster - FIRST_FAT) * FAT_PER_CLUSTER + half * (PAGE / 4);
        for (i = 0; i < PAGE / 4 && first + i < ALLOC_END; i++)
            put32(d + 4 * i, first + i == 0 ? 0xffffffff : 0x7fffffff);   /* root end, free */
        return 1;
    }
    if (cluster == ALLOC_OFFSET) {                    /* root directory */
        if (half == 0)
            fsentry(d, 0x8427, 2, ".");
        else
            fsentry(d, 0xa426, 0, "..");
        return 1;
    }
    return 0;
}

int vmc_format_image(const char *path)
{
    int fd, page, n, r = 0;

    fd = iomanX_open(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
    if (fd < 0)
        return fd;
    for (page = 0; page < TOTAL_PAGES && r >= 0; page += CHUNK_PAGES) {
        for (n = 0; n < CHUNK_PAGES; n++) {
            unsigned char *raw = g_chunk + n * RAW_PAGE;
            if (page_data(page + n, raw)) {
                ecc128(raw + PAGE, raw);
                ecc128(raw + PAGE + 3, raw + 128);
                ecc128(raw + PAGE + 6, raw + 256);
                ecc128(raw + PAGE + 9, raw + 384);
                memset(raw + PAGE + 12, 0, 4);
            } else {
                memset(raw + PAGE, 0xff, 16);
            }
        }
        r = iomanX_write(fd, g_chunk, sizeof(g_chunk));
        if (r >= 0 && r != (int)sizeof(g_chunk))
            r = -EIO;
    }
    iomanX_close(fd);
    return r < 0 ? r : 0;
}
