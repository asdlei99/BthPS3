/**********************************************************************************
 *                                                                                *
 * BthPS3 - Windows kernel-mode Bluetooth profile and bus driver                  *
 *                                                                                *
 * BSD 3-Clause License                                                           *
 *                                                                                *
 * Copyright (c) 2018-2020, Nefarius Software Solutions e.U.                      *
 * All rights reserved.                                                           *
 *                                                                                *
 * Redistribution and use in source and binary forms, with or without             *
 * modification, are permitted provided that the following conditions are met:    *
 *                                                                                *
 * 1. Redistributions of source code must retain the above copyright notice, this *
 *    list of conditions and the following disclaimer.                            *
 *                                                                                *
 * 2. Redistributions in binary form must reproduce the above copyright notice,   *
 *    this list of conditions and the following disclaimer in the documentation   *
 *    and/or other materials provided with the distribution.                      *
 *                                                                                *
 * 3. Neither the name of the copyright holder nor the names of its               *
 *    contributors may be used to endorse or promote products derived from        *
 *    this software without specific prior written permission.                    *
 *                                                                                *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"    *
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE      *
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE *
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE   *
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL     *
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR     *
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER     *
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,  *
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE  *
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.           *
 *                                                                                *
 **********************************************************************************/


#include "driver.h"
#include "device.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, BthPS3_CreateDevice)
#pragma alloc_text (PAGE, BthPS3_EvtWdfDeviceSelfManagedIoCleanup)
#pragma alloc_text (PAGE, BthPS3_OpenFilterIoTarget)
#endif


//
// Framework device creation entry point
// 
NTSTATUS
BthPS3_CreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    WDF_OBJECT_ATTRIBUTES           attributes;
    WDFDEVICE                       device;
    NTSTATUS                        status;
    WDF_PNPPOWER_EVENT_CALLBACKS    pnpPowerCallbacks;
    WDF_CHILD_LIST_CONFIG           childListCfg;
    PBTHPS3_SERVER_CONTEXT          pSrvCtx;

    PAGED_CODE();


    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "%!FUNC! Entry");

    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_BUS_EXTENDER);

    //
    // Prepare child list
    // 
    WDF_CHILD_LIST_CONFIG_INIT(
        &childListCfg,
        sizeof(PDO_IDENTIFICATION_DESCRIPTION),
        BthPS3_EvtWdfChildListCreateDevice
    );
    childListCfg.EvtChildListIdentificationDescriptionCompare =
        BthPS3_PDO_EvtChildListIdentificationDescriptionCompare;

    WdfFdoInitSetDefaultChildListConfig(DeviceInit,
        &childListCfg,
        WDF_NO_OBJECT_ATTRIBUTES
    );

    //
    // Configure PNP/power callbacks
    //
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDeviceSelfManagedIoInit = BthPS3_EvtWdfDeviceSelfManagedIoInit;
    pnpPowerCallbacks.EvtDeviceSelfManagedIoCleanup = BthPS3_EvtWdfDeviceSelfManagedIoCleanup;

    WdfDeviceInitSetPnpPowerEventCallbacks(
        DeviceInit,
        &pnpPowerCallbacks
    );

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, BTHPS3_SERVER_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE,
            "WdfDeviceCreate failed with status %!STATUS!", status);

        goto exit;
    }

    pSrvCtx = GetServerDeviceContext(device);

    status = BthPS3_ServerContextInit(pSrvCtx, device);

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE,
            "Initialization of context failed with status %!STATUS!", status);

        goto exit;
    }

    status = BthPS3QueueInitialize(device);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE,
            "BthPS3QueueInitialize failed with status %!STATUS!", status);
        goto exit;
    }

    //
    // Query for interfaces and pre-allocate BRBs
    //

    status = BthPS3_Initialize(GetServerDeviceContext(device));
    if (!NT_SUCCESS(status))
    {
        goto exit;
    }

    //
    // Search and open filter remote I/O target
    // 

    status = BthPS3_OpenFilterIoTarget(device);
    if (!NT_SUCCESS(status))
    {
        goto exit;
    }

    //
    // Allocate request object for async filter communication
    // 

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = pSrvCtx->PsmFilter.IoTarget;

    status = WdfRequestCreate(
        &attributes,
        pSrvCtx->PsmFilter.IoTarget,
        &pSrvCtx->PsmFilter.AsyncRequest
    );
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE,
            "WdfRequestCreate failed with status %!STATUS!", status);
        goto exit;
    }

exit:
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "%!FUNC! Exit");

    return status;
}

//
// Locate and attempt to open instance of BTHPS3PSM filter device
// 
NTSTATUS BthPS3_OpenFilterIoTarget(WDFDEVICE Device)
{
    NTSTATUS                    status = STATUS_UNSUCCESSFUL;
    WDF_OBJECT_ATTRIBUTES       ioTargetAttrib;
    PBTHPS3_SERVER_CONTEXT      pCtx;
    WDF_IO_TARGET_OPEN_PARAMS   openParams;

    DECLARE_CONST_UNICODE_STRING(symbolicLink, BTHPS3PSM_SYMBOLIC_NAME_STRING);

    PAGED_CODE();

    pCtx = GetServerDeviceContext(Device);

    WDF_OBJECT_ATTRIBUTES_INIT(&ioTargetAttrib);

    status = WdfIoTargetCreate(
        Device,
        &ioTargetAttrib,
        &pCtx->PsmFilter.IoTarget
    );
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_DEVICE,
            "WdfIoTargetCreate failed with status %!STATUS!",
            status
        );
        return status;
    }
    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(
        &openParams,
        &symbolicLink,
        STANDARD_RIGHTS_ALL
    );
    status = WdfIoTargetOpen(
        pCtx->PsmFilter.IoTarget,
        &openParams
    );
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_DEVICE,
            "WdfIoTargetOpen failed with status %!STATUS!",
            status
        );
        WdfObjectDelete(pCtx->PsmFilter.IoTarget);
        return status;
    }

    return status;
}

//
// Timed auto-reset of filter driver
// 
void BthPS3_EnablePatchEvtWdfTimer(
    WDFTIMER Timer
)
{
    NTSTATUS status;
    PBTHPS3_SERVER_CONTEXT devCtx = GetServerDeviceContext(WdfTimerGetParentObject(Timer));

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE,
        "%!FUNC! called, requesting filter to enable patch"
    );

    status = BthPS3PSM_EnablePatchAsync(
        devCtx->PsmFilter.IoTarget,
        0 // TODO: read from registry?
    );

    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE,
            "BthPS3PSM_EnablePatchAsync failed with status %!STATUS!",
            status
        );
    }
}

//
// Gets invoked on device power-up
// 
_Use_decl_annotations_
NTSTATUS
BthPS3_EvtWdfDeviceSelfManagedIoInit(
    WDFDEVICE  Device
)
{
    NTSTATUS status;
    PBTHPS3_SERVER_CONTEXT devCtx = GetServerDeviceContext(Device);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "%!FUNC! Entry");

    status = BthPS3_RetrieveLocalInfo(&devCtx->Header);
    if (!NT_SUCCESS(status))
    {
        goto exit;
    }

    status = BthPS3_RegisterPSM(devCtx);
    if (!NT_SUCCESS(status))
    {
        goto exit;
    }

    status = BthPS3_RegisterL2CAPServer(devCtx);
    if (!NT_SUCCESS(status))
    {
        goto exit;
    }

    //
    // Attempt to enable, but ignore failure
    //
    if (devCtx->Settings.AutoEnableFilter)
    {
        (void)BthPS3PSM_EnablePatchSync(
            devCtx->PsmFilter.IoTarget,
            0
        );
    }

exit:

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "%!FUNC! Exit");

    return status;
}

//
// Gets invoked on device shutdown
// 
_Use_decl_annotations_
VOID
BthPS3_EvtWdfDeviceSelfManagedIoCleanup(
    WDFDEVICE  Device
)
{
    PBTHPS3_SERVER_CONTEXT devCtx = GetServerDeviceContext(Device);
    WDFOBJECT currentItem;
    PBTHPS3_CLIENT_CONNECTION connection = NULL;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "%!FUNC! Entry");

    if (devCtx->PsmFilter.IoTarget != NULL)
    {
        WdfIoTargetClose(devCtx->PsmFilter.IoTarget);
        WdfObjectDelete(devCtx->PsmFilter.IoTarget);
    }

    if (NULL != devCtx->L2CAPServerHandle)
    {
        BthPS3_UnregisterL2CAPServer(devCtx);
    }

    if (0 != devCtx->PsmHidControl)
    {
        BthPS3_UnregisterPSM(devCtx);
    }

    //
    // Drop children
    // 
    // At this stage nobody is updating the connection list so no locking required
    // 
    while ((currentItem = WdfCollectionGetFirstItem(devCtx->ClientConnections)) != NULL)
    {
        WdfCollectionRemoveItem(devCtx->ClientConnections, 0);
        connection = GetClientConnection(currentItem);

        //
        // Disconnect HID Interrupt Channel first
        // 
        L2CAP_PS3_RemoteDisconnect(
            &devCtx->Header,
            connection->RemoteAddress,
            &connection->HidInterruptChannel
        );

        //
        // Wait until BTHPORT.SYS has completely dropped the connection
        // 
        KeWaitForSingleObject(
            &connection->HidInterruptChannel.DisconnectEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL
        );

        //
        // Disconnect HID Control Channel last
        // 
        L2CAP_PS3_RemoteDisconnect(
            &devCtx->Header,
            connection->RemoteAddress,
            &connection->HidControlChannel
        );

        //
        // Wait until BTHPORT.SYS has completely dropped the connection
        // 
        KeWaitForSingleObject(
            &connection->HidControlChannel.DisconnectEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL
        );

        //
        // Invokes freeing memory
        // 
        WdfObjectDelete(currentItem);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "%!FUNC! Exit");

    return;
}
