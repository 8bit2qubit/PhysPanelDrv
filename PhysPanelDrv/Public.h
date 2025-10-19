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

DEFINE_GUID (GUID_DEVINTERFACE_PhysPanelDrv,
    0x83b3003c,0xdc08,0x45aa,0xb0,0xc0,0xb8,0x8e,0xf1,0x3d,0xe4,0x04);
// {83b3003c-dc08-45aa-b0c0-b88ef13de404}
