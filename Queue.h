/*++

Module Name:

    queue.h

Abstract:

    This file contains the queue definitions.

Environment:

    Kernel-mode Driver Framework

--*/

EXTERN_C_START

//
// This is the context that can be placed per queue
// and would contain per queue information.
//
typedef struct _QUEUE_CONTEXT {

    ULONG PrivateDeviceData;  // just a placeholder

} QUEUE_CONTEXT, *PQUEUE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(QUEUE_CONTEXT, QueueGetContext)

NTSTATUS
mousetunerQueueInitialize(
    _In_ WDFDEVICE Device
    );

//
// Events from the IoQueue object
//
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL mousetunerEvtIoInternalDeviceControl;

NTSTATUS
mousetunerEvtIoInternalMouseConnect(
    _In_ WDFREQUEST Request,
    _In_ WDFDEVICE hDevice,
    _In_ PDEVICE_CONTEXT devExt
);
VOID
mousetunerDispatchPassThrough(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target
);
VOID
mousetunerServiceCallback(
    IN PDEVICE_OBJECT DeviceObject,
    IN PMOUSE_INPUT_DATA InputDataStart,
    IN PMOUSE_INPUT_DATA InputDataEnd,
    IN OUT PULONG InputDataConsumed
);

EXTERN_C_END
