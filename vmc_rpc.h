/* Shared EE/IOP protocol of the virtual memory card module (vmc.irx).
   The IOP side drives the SDK's vmcman.irx through iomanX: vmc0: is a real
   PS2 memory card filesystem inside an image file (PCSX2 layout, ECC
   included), here reached through ps2link's host: device. */
#ifndef PS2_VMC_RPC_H
#define PS2_VMC_RPC_H

#define PS2_VMC_RPC_SID    0x564d4331u  /* VMC1 */
#define PS2_VMC_CHUNK      4096u        /* data bytes per call */
#define PS2_VMC_PATH_MAX   128u
#define PS2_VMC_SLOTS      2            /* vmc0:, vmc1: */

/* iomanX open flags (the IOP fio values, also used by libmc's mcOpen). */
#define PS2_VMC_O_RDONLY   0x0001
#define PS2_VMC_O_WRONLY   0x0002
#define PS2_VMC_O_CREAT    0x0200
#define PS2_VMC_O_TRUNC    0x0400

enum {
    PS2_VMC_MOUNT = 1,  /* arg: slot, path: formatted image; result 0 mounted, <0 error */
    PS2_VMC_OPEN  = 2,  /* path, arg: flags; result fd */
    PS2_VMC_READ  = 3,  /* arg: fd, len; result bytes, data in the reply */
    PS2_VMC_WRITE = 4,  /* arg: fd, len, data; result bytes */
    PS2_VMC_CLOSE = 5,  /* arg: fd */
    PS2_VMC_MKDIR = 6,  /* path */
    PS2_VMC_REMOUNT = 7,/* arg: slot; remount its image, whose host file handle died
                           with a ps2client session; result as MOUNT */
    PS2_VMC_FORMAT = 8  /* path: image to create as a new formatted 8 MB card;
                           the caller checks it is absent; result 0 or <0 */
};

typedef struct {
    int cmd;
    int arg;
    int len;
    int pad;
    char path[PS2_VMC_PATH_MAX];
    unsigned char data[PS2_VMC_CHUNK];
} Ps2VmcRequest;

typedef struct {
    int result;
    int pad[3];
    unsigned char data[PS2_VMC_CHUNK];
} Ps2VmcReply;

/* Bytes that cross the SIF per call: the header, plus data only where used. */
#define PS2_VMC_REQUEST_HEADER (16u + PS2_VMC_PATH_MAX)
#define PS2_VMC_REPLY_HEADER   16u

#endif
