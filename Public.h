/*++

Module Name:

    public.h

Abstract:

    This module contains the common declarations shared by driver
    and user applications.

Environment:

    user and kernel

--*/

//
// Define an Interface Guid so that apps can find the device and talk to it.
//

DEFINE_GUID (GUID_DEVINTERFACE_mousetuner,
    0x8aeda6ad,0xa477,0x4a2a,0xa8,0xae,0x9a,0x40,0xad,0x57,0xb7,0x96);
// {8aeda6ad-a477-4a2a-a8ae-9a40ad57b796}
