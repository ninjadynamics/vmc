/* Virtual memory card RPC server (vmc_rpc.h).
 *
 * The EE loads the SDK's vmcman.irx (mcman's filesystem over an iomanX file
 * instead of SIO2), then this module. MOUNT binds vmc0: to an image; the file
 * calls are plain iomanX operations on vmc0: paths, run on this module's RPC
 * thread. Writes land in the image file page by page (512 data + 16 ECC),
 * so the same file opens in PCSX2 and in the SDK's vmc tools.
 *
 * A fresh image from tools/vmc/mkcard.py carries a valid superblock with an
 * empty indirect-FAT list (ifc_list[0] = 0xffffffff); a formatted card always
 * has one. MOUNT formats only such an image, so a real save card is never
 * formatted by accident.
 */
#include "irx_imports.h"
#include "vmc_rpc.h"

#define PS2_VMC_THREAD_PRIORITY 0x60
#define PS2_VMC_THREAD_STACK    0x1000
#define PS2_VMC_IFC_LIST_OFFSET 0x50

static SifRpcDataQueue_t g_queue;
static SifRpcServerData_t g_server;
static Ps2VmcRequest g_request __attribute__((aligned(64)));
static Ps2VmcReply g_reply __attribute__((aligned(64)));

static int vmc_image_is_fresh(const char *path)
{
    unsigned char sb[PS2_VMC_IFC_LIST_OFFSET + 4];
    const int fd = iomanX_open(path, FIO_O_RDONLY, 0);
    int n;

    if (fd < 0)
        return fd;
    n = iomanX_read(fd, sb, sizeof(sb));
    iomanX_close(fd);
    if (n != (int)sizeof(sb))
        return -EIO;
    return sb[PS2_VMC_IFC_LIST_OFFSET] == 0xff && sb[PS2_VMC_IFC_LIST_OFFSET + 1] == 0xff &&
           sb[PS2_VMC_IFC_LIST_OFFSET + 2] == 0xff && sb[PS2_VMC_IFC_LIST_OFFSET + 3] == 0xff;
}

static int vmc_mount(const char *path)
{
    const int fresh = vmc_image_is_fresh(path);
    int r;

    if (fresh < 0)
        return fresh;
    r = iomanX_mount("vmc0:", path, 0, NULL, 0);
    if (r < 0 || !fresh)
        return r;
    r = iomanX_format("vmc0:", NULL, NULL, 0);
    return r < 0 ? r : 1;
}

static void *rpc_handler(int fn, void *buffer, int size)
{
    Ps2VmcRequest *req = (Ps2VmcRequest *)buffer;
    unsigned int len;

    (void)size;
    req->path[PS2_VMC_PATH_MAX - 1] = '\0';
    len = (unsigned int)req->len <= PS2_VMC_CHUNK ? (unsigned int)req->len : PS2_VMC_CHUNK;
    switch (fn) {
    case PS2_VMC_MOUNT:
        g_reply.result = vmc_mount(req->path);
        break;
    case PS2_VMC_OPEN:
        g_reply.result = iomanX_open(req->path, req->arg, 0666);
        break;
    case PS2_VMC_READ:
        g_reply.result = iomanX_read(req->arg, g_reply.data, len);
        break;
    case PS2_VMC_WRITE:
        g_reply.result = iomanX_write(req->arg, req->data, len);
        break;
    case PS2_VMC_CLOSE:
        g_reply.result = iomanX_close(req->arg);
        break;
    case PS2_VMC_MKDIR:
        g_reply.result = iomanX_mkdir(req->path, 0777);
        break;
    default:
        g_reply.result = -EINVAL;
        break;
    }
    return &g_reply;
}

static void rpc_thread(void *arg)
{
    (void)arg;
    sceSifRpcLoop(&g_queue);
}

int _start(int argc, char *argv[])
{
    iop_thread_t thread;
    int rpc;

    (void)argc;
    (void)argv;
    memset(&thread, 0, sizeof(thread));
    thread.attr = TH_C;
    thread.thread = rpc_thread;
    thread.stacksize = PS2_VMC_THREAD_STACK;
    thread.priority = PS2_VMC_THREAD_PRIORITY;
    rpc = CreateThread(&thread);
    if (rpc < 0)
        return MODULE_NO_RESIDENT_END;
    /* Registered before _start returns (readiness for the EE's bind). */
    sceSifSetRpcQueue(&g_queue, rpc);
    sceSifRegisterRpc(&g_server, PS2_VMC_RPC_SID, rpc_handler, &g_request, NULL, NULL, &g_queue);
    if (StartThread(rpc, NULL) < 0) {
        sceSifRemoveRpc(&g_server, &g_queue);
        sceSifRemoveRpcQueue(&g_queue);
        DeleteThread(rpc);
        return MODULE_NO_RESIDENT_END;
    }
    return MODULE_RESIDENT_END;
}
