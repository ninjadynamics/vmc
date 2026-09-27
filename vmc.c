/* Virtual memory card RPC server (vmc_rpc.h).
 *
 * The EE loads vmcman.irx (mcman's filesystem over an iomanX file instead of
 * SIO2), then this module. MOUNT binds a slot's device (vmc0:, vmc1:) to a
 * card image, formatted beforehand by tools/vmc/ps2mc.py or by FORMAT here
 * (vmcman mounts only formatted cards); the file calls are plain iomanX operations on those
 * paths, run on this module's RPC thread. Writes land in the image page by
 * page (512 data + 16 ECC), so the same file opens in PCSX2. REMOUNT reopens
 * a slot's image after the host session that owned its file handle went
 * away (a ps2client reattach).
 */
#include "irx_imports.h"
#include "vmc_rpc.h"
#include "vmc_format.h"

#define PS2_VMC_THREAD_PRIORITY 0x60
#define PS2_VMC_THREAD_STACK    0x1000
static SifRpcDataQueue_t g_queue;
static SifRpcServerData_t g_server;
static Ps2VmcRequest g_request __attribute__((aligned(64)));
static Ps2VmcReply g_reply __attribute__((aligned(64)));
static char g_image[PS2_VMC_SLOTS][PS2_VMC_PATH_MAX];   /* mounted images, for REMOUNT */

/* "vmc0:" / "vmc1:" */
static const char *vmc_device(int slot)
{
    static char dev[] = "vmc0:";
    dev[3] = (char)('0' + slot);
    return dev;
}

static int vmc_mount(int slot, const char *path)
{
    const int r = iomanX_mount(vmc_device(slot), path, 0, NULL, 0);
    if (r >= 0 && path != g_image[slot])
        strcpy(g_image[slot], path);
    return r;
}

static void *rpc_handler(int fn, void *buffer, int size)
{
    Ps2VmcRequest *req = (Ps2VmcRequest *)buffer;
    unsigned int len;

    (void)size;
    req->path[PS2_VMC_PATH_MAX - 1] = '\0';
    len = (unsigned int)req->len <= PS2_VMC_CHUNK ? (unsigned int)req->len : PS2_VMC_CHUNK;
    if ((fn == PS2_VMC_MOUNT || fn == PS2_VMC_REMOUNT) &&
        (req->arg < 0 || req->arg >= PS2_VMC_SLOTS)) {
        g_reply.result = -EINVAL;
        return &g_reply;
    }
    switch (fn) {
    case PS2_VMC_MOUNT:
        g_reply.result = vmc_mount(req->arg, req->path);
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
    case PS2_VMC_REMOUNT:
        /* A host: handle belongs to the ps2client session that opened it;
           after a reattach the old one fails every page. */
        iomanX_umount(vmc_device(req->arg));
        g_reply.result = g_image[req->arg][0] ? vmc_mount(req->arg, g_image[req->arg]) : -ENXIO;
        break;
    case PS2_VMC_FORMAT:
        g_reply.result = vmc_format_image(req->path);
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
