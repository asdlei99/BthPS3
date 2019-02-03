/*
* BthPS3 - Windows kernel-mode Bluetooth profile and bus driver
* Copyright (C) 2018-2019  Nefarius Software Solutions e.U. and Contributors
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#pragma once

EXTERN_C_START

struct _BTHPS3_CLIENT_CONNECTION;

//
// Request context space for forwarded requests (PDO to FDO)
// 
typedef struct _BTHPS3_FDO_PDO_REQUEST_CONTEXT
{
    //
    // Client connection context
    // 
    struct _BTHPS3_CLIENT_CONNECTION *ClientConnection;

} BTHPS3_FDO_PDO_REQUEST_CONTEXT, *PBTHPS3_FDO_PDO_REQUEST_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(BTHPS3_FDO_PDO_REQUEST_CONTEXT, GetFdoPdoRequestContext)

NTSTATUS
BthPS3QueueInitialize(
    _In_ WDFDEVICE Device
    );

//
// Events from the IoQueue object
//
EVT_WDF_IO_QUEUE_IO_STOP BthPS3_EvtIoStop;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL BthPS3_EvtWdfIoQueueIoInternalDeviceControl;

EXTERN_C_END
