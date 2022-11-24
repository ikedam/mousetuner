/*++

Module Name:

    queue.c

Abstract:

    This file contains the queue entry points and callbacks.

Environment:

    Kernel-mode Driver Framework

--*/

#include "driver.h"
#include "queue.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, mousetunerQueueInitialize)
#pragma alloc_text (PAGE, mousetunerEvtIoInternalDeviceControl)
#endif

NTSTATUS
mousetunerQueueInitialize(
    _In_ WDFDEVICE Device
    )
/*++

Routine Description:

     The I/O dispatch callbacks for the frameworks device object
     are configured in this function.

     A single default I/O Queue is configured for parallel request
     processing, and a driver context memory allocation is created
     to hold our structure QUEUE_CONTEXT.

Arguments:

    Device - Handle to a framework device object.

Return Value:

    VOID

--*/
{
    NTSTATUS status;
    WDF_IO_QUEUE_CONFIG queueConfig;

    PAGED_CODE();

    //
    // Configure a default queue so that requests that are not
    // configure-fowarded using WdfDeviceConfigureRequestDispatching to goto
    // other queues get dispatched here.
    //
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
         &queueConfig,
        WdfIoQueueDispatchParallel
        );

    queueConfig.EvtIoInternalDeviceControl = mousetunerEvtIoInternalDeviceControl;

    status = WdfIoQueueCreate(
                 Device,
                 &queueConfig,
                 WDF_NO_OBJECT_ATTRIBUTES,
                 WDF_NO_HANDLE // default queue
                 );

    if(!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate failed %!STATUS!", status);
        return status;
    }

    return status;
}

VOID
mousetunerEvtIoInternalDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
/*++

Routine Description:

    This routine is the dispatch routine for internal device control requests.
    There are two specific control codes that are of interest:

    IOCTL_INTERNAL_MOUSE_CONNECT:
        Store the old context and function pointer and replace it with our own.
        This makes life much simpler than intercepting IRPs sent by the RIT and
        modifying them on the way back up.

    IOCTL_INTERNAL_I8042_HOOK_MOUSE:
        Add in the necessary function pointers and context values so that we can
        alter how the ps/2 mouse is initialized.

    NOTE:  Handling IOCTL_INTERNAL_I8042_HOOK_MOUSE is *NOT* necessary if
           all you want to do is filter MOUSE_INPUT_DATAs.  You can remove
           the handling code and all related device extension fields and
           functions to conserve space.

Arguments:

    Queue -  Handle to the framework queue object that is associated with the
             I/O request.

    Request - Handle to a framework request object.

    OutputBufferLength - Size of the output buffer in bytes

    InputBufferLength - Size of the input buffer in bytes

    IoControlCode - I/O control code.

Return Value:

    VOID

--*/
{
    WDFDEVICE hDevice;
    PDEVICE_CONTEXT devExt;
    NTSTATUS status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    PAGED_CODE();

    hDevice = WdfIoQueueGetDevice(Queue);
    devExt = DeviceGetContext(hDevice);

    switch (IoControlCode) {
    case IOCTL_INTERNAL_MOUSE_CONNECT:
        status = mousetunerEvtIoInternalMouseConnect(
            Request,
            hDevice,
            devExt
        );
        break;
    //
    // Disconnect a mouse class device driver from the port driver.
    //
    case IOCTL_INTERNAL_MOUSE_DISCONNECT:
        status = STATUS_NOT_IMPLEMENTED;
        break;
    }

    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    mousetunerDispatchPassThrough(Request, WdfDeviceGetIoTarget(hDevice));
}

NTSTATUS
mousetunerEvtIoInternalMouseConnect(
    _In_ WDFREQUEST Request,
    _In_ WDFDEVICE hDevice,
    _In_ PDEVICE_CONTEXT devExt
)
{
    NTSTATUS status;
    PCONNECT_DATA connectData;
    size_t length;

    //
    // Only allow one connection.
    //
    if (devExt->UpperConnectData.ClassService != NULL) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "Duplicated connection");
        return STATUS_SHARING_VIOLATION;
    }

    //
    // Copy the connection parameters to the device extension.
    //
    status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(CONNECT_DATA),
        &connectData,
        &length
    );
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfRequestRetrieveInputBuffer failed %!STATUS!", status);
        return status;
    }

    devExt->UpperConnectData = *connectData;

    //
    // Hook into the report chain.  Everytime a mouse packet is reported to
    // the system, MouFilter_ServiceCallback will be called
    //
    connectData->ClassDeviceObject = WdfDeviceWdmGetDeviceObject(hDevice);
    connectData->ClassService = (PVOID)mousetunerServiceCallback;

    return STATUS_SUCCESS;
}


VOID
mousetunerDispatchPassThrough(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target
)
/*++
Routine Description:

    Passes a request on to the lower driver.
 
--*/
{
    //
    // Pass the IRP to the target
    //

    WDF_REQUEST_SEND_OPTIONS options;
    BOOLEAN ret;
    NTSTATUS status = STATUS_SUCCESS;

    //
    // We are not interested in post processing the IRP so 
    // fire and forget.
    //
    WDF_REQUEST_SEND_OPTIONS_INIT(
        &options,
        WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET
    );

    ret = WdfRequestSend(Request, Target, &options);

    if (ret == FALSE) {
        status = WdfRequestGetStatus(Request);
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfRequestSend failed %!STATUS!", status);
        WdfRequestComplete(Request, status);
    }

    return;
}

VOID
mousetunerServiceCallback(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PMOUSE_INPUT_DATA InputDataStart,
    _In_ PMOUSE_INPUT_DATA InputDataEnd,
    _Inout_ PULONG InputDataConsumed
)
/*++
https://learn.microsoft.com/en-us/previous-versions/ff542380(v=vs.85)
Routine Description:

    Called when there are mouse packets to report to the RIT.  You can do
    anything you like to the packets.  For instance:

    o Drop a packet altogether
    o Mutate the contents of a packet
    o Insert packets into the stream

Arguments:

    DeviceObject - Context passed during the connect IOCTL

    InputDataStart - First packet to be reported

    InputDataEnd - One past the last packet to be reported.  Total number of
                   packets is equal to InputDataEnd - InputDataStart

    InputDataConsumed - Set to the total number of packets consumed by the RIT
                        (via the function pointer we replaced in the connect
                        IOCTL)

Return Value:

    Status is returned.

--*/
{
    PDEVICE_CONTEXT devExt;
    WDFDEVICE hDevice;

    for (PMOUSE_INPUT_DATA data = InputDataStart; data < InputDataEnd; ++data) {
        USHORT newButtonFlags = data->ButtonFlags & ~(MOUSE_LEFT_BUTTON_DOWN | MOUSE_LEFT_BUTTON_UP | MOUSE_RIGHT_BUTTON_DOWN | MOUSE_RIGHT_BUTTON_UP);
        if (data->ButtonFlags & MOUSE_LEFT_BUTTON_DOWN) {
            newButtonFlags |= MOUSE_RIGHT_BUTTON_DOWN;
        }
        if (data->ButtonFlags & MOUSE_LEFT_BUTTON_UP) {
            newButtonFlags |= MOUSE_RIGHT_BUTTON_UP;
        }
        if (data->ButtonFlags & MOUSE_RIGHT_BUTTON_DOWN) {
            newButtonFlags |= MOUSE_LEFT_BUTTON_DOWN;
        }
        if (data->ButtonFlags & MOUSE_RIGHT_BUTTON_UP) {
            newButtonFlags |= MOUSE_LEFT_BUTTON_UP;
        }
        data->ButtonFlags = newButtonFlags;

        data->LastX /= 3;
        data->LastY /= 3;
    }

    hDevice = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);

    devExt = DeviceGetContext(hDevice);
    //
    // UpperConnectData must be called at DISPATCH
    //
    (*(PSERVICE_CALLBACK_ROUTINE)devExt->UpperConnectData.ClassService)(
        devExt->UpperConnectData.ClassDeviceObject,
        InputDataStart,
        InputDataEnd,
        InputDataConsumed
    );
}
