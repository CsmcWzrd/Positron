#include <ntddk.h>
#include <wdf.h>
#include <math.h>
#include "../include/xchg_proto.h"
#include "xchg_ioctl_windows.h"

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD XchgEvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL XchgEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_READ XchgEvtIoRead;
EVT_WDF_IO_QUEUE_IO_WRITE XchgEvtIoWrite;
EVT_WDF_FILE_CREATE XchgEvtFileCreate;
EVT_WDF_FILE_CLOSE XchgEvtFileClose;
EVT_WDF_TIMER XchgEvtTimerFunc;

typedef struct _XCHG_REGION {
    PUCHAR Buffer;
    ULONG RegionSize;
    ULONG PayloadSize;
    ULONG ReadPos[2];
} XCHG_REGION, *PXCHG_REGION;

typedef struct _XCHG_DEVICE_CONTEXT {
    WDFWAITLOCK Lock;
    xchg_config_t Config;
    XCHG_REGION Ch0To1;
    XCHG_REGION Ch1To0;
    BOOLEAN Opened[2];
    ULONGLONG EpochId;
    LARGE_INTEGER EpochPeriod100ns;
    WDFTIMER Timer;
} XCHG_DEVICE_CONTEXT, *PXCHG_DEVICE_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(XCHG_DEVICE_CONTEXT, XchgGetDeviceContext)

typedef struct _XCHG_FILE_CONTEXT {
    ULONG Role;
} XCHG_FILE_CONTEXT, *PXCHG_FILE_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(XCHG_FILE_CONTEXT, XchgGetFileContext)

static double ComputePeriodSeconds(USHORT mantissa, INT exponent) {
    double log10T;
    if (mantissa == 0) return 1.0;
    if (exponent < XCHG_MIN_EXPONENT) exponent = XCHG_MIN_EXPONENT;
    if (exponent > XCHG_MAX_EXPONENT) exponent = XCHG_MAX_EXPONENT;
    log10T = log10((double)mantissa) + (double)exponent;
    if (log10T < log10(XCHG_MIN_PERIOD_SEC)) return XCHG_MIN_PERIOD_SEC;
    if (log10T > log10(XCHG_MAX_PERIOD_SEC)) return XCHG_MAX_PERIOD_SEC;
    return pow(10.0, log10T);
}

static LARGE_INTEGER ComputePeriod100ns(USHORT mantissa, INT exponent) {
    LARGE_INTEGER li;
    double s = ComputePeriodSeconds(mantissa, exponent);
    li.QuadPart = -(LONGLONG)(s * 10000000.0);
    return li;
}

static VOID RegionPublishSize(PXCHG_REGION r) {
    if (r->Buffer && r->RegionSize >= sizeof(ULONG)) *(ULONG*)r->Buffer = r->PayloadSize;
}

static NTSTATUS RegionAlloc(PXCHG_REGION r, ULONG size) {
    PUCHAR p;
    if (size < sizeof(ULONG)) return STATUS_INVALID_PARAMETER;
    p = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, size, 'ghcX');
    if (!p) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(p, size);
    if (r->Buffer) ExFreePool(r->Buffer);
    r->Buffer = p;
    r->RegionSize = size;
    r->PayloadSize = 0;
    r->ReadPos[0] = r->ReadPos[1] = 0;
    RegionPublishSize(r);
    return STATUS_SUCCESS;
}

static PXCHG_REGION TxRegionForRole(PXCHG_DEVICE_CONTEXT ctx, ULONG role) { return (role == 0) ? &ctx->Ch0To1 : &ctx->Ch1To0; }
static PXCHG_REGION RxRegionForRole(PXCHG_DEVICE_CONTEXT ctx, ULONG role) { return (role == 0) ? &ctx->Ch1To0 : &ctx->Ch0To1; }

VOID XchgEvtTimerFunc(WDFTIMER Timer) {
    WDFDEVICE dev = WdfTimerGetParentObject(Timer);
    PXCHG_DEVICE_CONTEXT ctx = XchgGetDeviceContext(dev);
    WdfWaitLockAcquire(ctx->Lock, NULL);
    ctx->EpochId++;
    ctx->Ch0To1.ReadPos[0] = ctx->Ch0To1.ReadPos[1] = 0;
    ctx->Ch1To0.ReadPos[0] = ctx->Ch1To0.ReadPos[1] = 0;
    WdfWaitLockRelease(ctx->Lock);
    WdfTimerStart(ctx->Timer, ctx->EpochPeriod100ns.QuadPart);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, XchgEvtDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

NTSTATUS XchgEvtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit) {
    UNREFERENCED_PARAMETER(Driver);
    WDF_OBJECT_ATTRIBUTES devAttr, fileAttr, timerAttr;
    WDFDEVICE device;
    PXCHG_DEVICE_CONTEXT ctx;
    NTSTATUS status;
    WDF_IO_QUEUE_CONFIG qcfg;
    WDF_FILEOBJECT_CONFIG fcfg;
    WDF_TIMER_CONFIG tcfg;
    DECLARE_CONST_UNICODE_STRING(devName, L"\\Device\\Xchg0");
    DECLARE_CONST_UNICODE_STRING(symLink, L"\\DosDevices\\Xchg0");

    WdfDeviceInitAssignName(DeviceInit, &devName);
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetExclusive(DeviceInit, FALSE);
    WDF_FILEOBJECT_CONFIG_INIT(&fcfg, XchgEvtFileCreate, XchgEvtFileClose, WDF_NO_EVENT_CALLBACK);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&fileAttr, XCHG_FILE_CONTEXT);
    WdfDeviceInitSetFileObjectConfig(DeviceInit, &fcfg, &fileAttr);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&devAttr, XCHG_DEVICE_CONTEXT);
    status = WdfDeviceCreate(&DeviceInit, &devAttr, &device);
    if (!NT_SUCCESS(status)) return status;
    status = WdfDeviceCreateSymbolicLink(device, &symLink);
    if (!NT_SUCCESS(status)) return status;

    ctx = XchgGetDeviceContext(device);
    RtlZeroMemory(ctx, sizeof(*ctx));
    status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &ctx->Lock);
    if (!NT_SUCCESS(status)) return status;
    ctx->Config.mantissa = 1;
    ctx->Config.exponent = 0;
    ctx->Config.send_region_size = 4096;
    ctx->Config.recv_region_size = 4096;
    ctx->EpochPeriod100ns = ComputePeriod100ns(1, 0);
    status = RegionAlloc(&ctx->Ch0To1, ctx->Config.send_region_size);
    if (!NT_SUCCESS(status)) return status;
    status = RegionAlloc(&ctx->Ch1To0, ctx->Config.recv_region_size);
    if (!NT_SUCCESS(status)) return status;

    WDF_TIMER_CONFIG_INIT(&tcfg, XchgEvtTimerFunc);
    WDF_OBJECT_ATTRIBUTES_INIT(&timerAttr);
    timerAttr.ParentObject = device;
    status = WdfTimerCreate(&tcfg, &timerAttr, &ctx->Timer);
    if (!NT_SUCCESS(status)) return status;
    WdfTimerStart(ctx->Timer, ctx->EpochPeriod100ns.QuadPart);

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&qcfg, WdfIoQueueDispatchSequential);
    qcfg.EvtIoDeviceControl = XchgEvtIoDeviceControl;
    qcfg.EvtIoRead = XchgEvtIoRead;
    qcfg.EvtIoWrite = XchgEvtIoWrite;
    return WdfIoQueueCreate(device, &qcfg, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
}

VOID XchgEvtFileCreate(WDFDEVICE Device, WDFREQUEST Request, WDFFILEOBJECT FileObject) {
    PXCHG_DEVICE_CONTEXT ctx = XchgGetDeviceContext(Device);
    PXCHG_FILE_CONTEXT fctx = XchgGetFileContext(FileObject);
    NTSTATUS status = STATUS_SUCCESS;
    WdfWaitLockAcquire(ctx->Lock, NULL);
    if (!ctx->Opened[0]) { ctx->Opened[0] = TRUE; fctx->Role = 0; }
    else if (!ctx->Opened[1]) { ctx->Opened[1] = TRUE; fctx->Role = 1; }
    else { status = STATUS_SHARING_VIOLATION; }
    WdfWaitLockRelease(ctx->Lock);
    WdfRequestComplete(Request, status);
}

VOID XchgEvtFileClose(WDFFILEOBJECT FileObject) {
    WDFDEVICE dev = WdfFileObjectGetDevice(FileObject);
    PXCHG_DEVICE_CONTEXT ctx = XchgGetDeviceContext(dev);
    PXCHG_FILE_CONTEXT fctx = XchgGetFileContext(FileObject);
    WdfWaitLockAcquire(ctx->Lock, NULL);
    ctx->Opened[fctx->Role] = FALSE;
    WdfWaitLockRelease(ctx->Lock);
}

VOID XchgEvtIoRead(WDFQUEUE Queue, WDFREQUEST Request, size_t Length) {
    WDFDEVICE dev = WdfIoQueueGetDevice(Queue);
    PXCHG_DEVICE_CONTEXT ctx = XchgGetDeviceContext(dev);
    WDFFILEOBJECT fobj = WdfRequestGetFileObject(Request);
    PXCHG_FILE_CONTEXT fctx = XchgGetFileContext(fobj);
    PXCHG_REGION r = RxRegionForRole(ctx, fctx->Role);
    PVOID outBuf; size_t outLen; ULONG avail, toCopy;
    if (!NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, 1, &outBuf, &outLen))) { WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL); return; }
    WdfWaitLockAcquire(ctx->Lock, NULL);
    if (r->PayloadSize == 0 || r->ReadPos[fctx->Role] >= r->PayloadSize) { WdfWaitLockRelease(ctx->Lock); WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, 0); return; }
    avail = r->PayloadSize - r->ReadPos[fctx->Role];
    toCopy = (ULONG)((Length < avail) ? Length : avail);
    if (toCopy > outLen) toCopy = (ULONG)outLen;
    RtlCopyMemory(outBuf, r->Buffer + sizeof(ULONG) + r->ReadPos[fctx->Role], toCopy);
    r->ReadPos[fctx->Role] += toCopy;
    WdfWaitLockRelease(ctx->Lock);
    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, toCopy);
}

VOID XchgEvtIoWrite(WDFQUEUE Queue, WDFREQUEST Request, size_t Length) {
    WDFDEVICE dev = WdfIoQueueGetDevice(Queue);
    PXCHG_DEVICE_CONTEXT ctx = XchgGetDeviceContext(dev);
    WDFFILEOBJECT fobj = WdfRequestGetFileObject(Request);
    PXCHG_FILE_CONTEXT fctx = XchgGetFileContext(fobj);
    PXCHG_REGION r = TxRegionForRole(ctx, fctx->Role);
    PVOID inBuf; size_t inLen; ULONG cap, toCopy;
    if (!NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, 1, &inBuf, &inLen))) { WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL); return; }
    WdfWaitLockAcquire(ctx->Lock, NULL);
    cap = r->RegionSize - sizeof(ULONG);
    toCopy = (ULONG)((Length < cap) ? Length : cap);
    if (toCopy > inLen) toCopy = (ULONG)inLen;
    RtlCopyMemory(r->Buffer + sizeof(ULONG), inBuf, toCopy);
    r->PayloadSize = toCopy;
    RegionPublishSize(r);
    if (fctx->Role == 0) r->ReadPos[1] = 0; else r->ReadPos[0] = 0;
    WdfWaitLockRelease(ctx->Lock);
    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, toCopy);
}

VOID XchgEvtIoDeviceControl(WDFQUEUE Queue, WDFREQUEST Request, size_t OutputBufferLength, size_t InputBufferLength, ULONG IoControlCode) {
    WDFDEVICE dev = WdfIoQueueGetDevice(Queue);
    PXCHG_DEVICE_CONTEXT ctx = XchgGetDeviceContext(dev);
    NTSTATUS status = STATUS_SUCCESS; size_t info = 0;
    UNREFERENCED_PARAMETER(OutputBufferLength); UNREFERENCED_PARAMETER(InputBufferLength);
    switch (IoControlCode) {
        case IOCTL_XCHG_SET_CONFIG: {
            xchg_config_t *cfg;
            status = WdfRequestRetrieveInputBuffer(Request, sizeof(*cfg), (PVOID*)&cfg, NULL);
            if (!NT_SUCCESS(status)) break;
            if (cfg->exponent < XCHG_MIN_EXPONENT || cfg->exponent > XCHG_MAX_EXPONENT || cfg->send_region_size < sizeof(ULONG) || cfg->recv_region_size < sizeof(ULONG)) { status = STATUS_INVALID_PARAMETER; break; }
            WdfWaitLockAcquire(ctx->Lock, NULL);
            status = RegionAlloc(&ctx->Ch0To1, cfg->send_region_size);
            if (NT_SUCCESS(status)) status = RegionAlloc(&ctx->Ch1To0, cfg->recv_region_size);
            if (NT_SUCCESS(status)) { ctx->Config = *cfg; ctx->EpochId = 0; ctx->EpochPeriod100ns = ComputePeriod100ns(cfg->mantissa, cfg->exponent); WdfTimerStart(ctx->Timer, ctx->EpochPeriod100ns.QuadPart); }
            WdfWaitLockRelease(ctx->Lock);
            break;
        }
        case IOCTL_XCHG_GET_STATUS: {
            xchg_status_t *st;
            status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*st), (PVOID*)&st, NULL);
            if (!NT_SUCCESS(status)) break;
            WdfWaitLockAcquire(ctx->Lock, NULL);
            RtlZeroMemory(st, sizeof(*st));
            st->mantissa = ctx->Config.mantissa;
            st->exponent = ctx->Config.exponent;
            st->send_region_size = ctx->Config.send_region_size;
            st->recv_region_size = ctx->Config.recv_region_size;
            st->tx_payload_size = ctx->Ch0To1.PayloadSize;
            st->rx_payload_size = ctx->Ch1To0.PayloadSize;
            st->epoch_id = ctx->EpochId;
            WdfWaitLockRelease(ctx->Lock);
            info = sizeof(*st);
            break;
        }
        case IOCTL_XCHG_RESET:
            WdfWaitLockAcquire(ctx->Lock, NULL);
            ctx->EpochId = 0; ctx->Ch0To1.PayloadSize = 0; ctx->Ch1To0.PayloadSize = 0; RegionPublishSize(&ctx->Ch0To1); RegionPublishSize(&ctx->Ch1To0); ctx->Ch0To1.ReadPos[0] = ctx->Ch0To1.ReadPos[1] = 0; ctx->Ch1To0.ReadPos[0] = ctx->Ch1To0.ReadPos[1] = 0;
            WdfWaitLockRelease(ctx->Lock);
            break;
        default: status = STATUS_INVALID_DEVICE_REQUEST; break;
    }
    WdfRequestCompleteWithInformation(Request, status, info);
}
