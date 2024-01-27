#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/PciRootBridgeIo.h>

//insert after PCIBus DXE Module: 3C1DE39F-D207-408A-AACC-731CFB7F1DD7

static EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_IO_MEM original_PciRootBridgeIo_Read;

EFI_STATUS EFIAPI new_PciRootBridgeIo_Read (
  IN EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL		*This,
  IN EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH	Width,
  IN UINT64									Address,
  IN UINTN									Count,
  IN OUT VOID								*Buffer )
{
	EFI_STATUS status;

	UINTN reg = (Address & 0xffffffff00000000) >> 32;  //ExtendedRegister
    UINTN bus = (Address & 0xff000000) >> 24;
    UINTN dev = (Address & 0xff0000) >> 16;
    UINTN func = (Address & 0xff00) >> 8;
    UINTN length = (1 << Width) * Count;
    UINT8* buffer8 = (UINT8*)Buffer;
    UINT16* buffer16 = (UINT16*)Buffer;
    UINT32* buffer32 = (UINT32*)Buffer;
    UINT16 vid, class;

    if (reg == 0)
    	reg = Address & 0xff;  //if ExtendedRegister == 0 use Register

    original_PciRootBridgeIo_Read(This, EfiPciWidthUint16, EFI_PCI_ADDRESS(bus, dev, func, 0x0), 1, &vid);
    original_PciRootBridgeIo_Read(This, EfiPciWidthUint16, EFI_PCI_ADDRESS(bus, dev, func, 0xA), 1, &class);

    status = original_PciRootBridgeIo_Read(This, Width, Address, Count, Buffer);  //call the original pciRootBridgeIo->Pci.Read Handler
    if (EFI_ERROR(status))
    	return status;

    if (class == 0x0300)  //VGA-compatible controller
    {
    	UINTN reg_bar;
    	if (vid == 0x10DE)  //|NVIDIA| I/O Ports: BAR5
    		reg_bar = 0x24;
    	else if (vid == 0x1002)  //[AMD/ATI]  I/O Ports: BAR4
    		reg_bar = 0x20;
    	else
    		return status;  //return for other VGAs (i.e. BMC VGA)

    	if (reg <= 0x0A && reg+length > 0x0A)
    		buffer8[0xA-reg] = 0x02; //spoof Sub-Class to be 3D controller instead of VGA-compatible controller

    	if (reg <= reg_bar && reg+length > reg_bar)
    	{
    		UINTN position = reg_bar-reg;  //spoof obsolete and unused 16-bit I/O Port BAR to 0x00000000, disabling enumeration
    		if (length-position >= 4)
    			buffer32[position/4] = 0x00000000;
    		if (length-position == 2)
    			buffer16[position/2] = 0x0000;
    		if (length-position == 1)
    			buffer8[position] = 0x00;
    	}
    }
	return status;
}

EFI_STATUS EFIAPI CPayneFixVGAEnumEntry (
  IN EFI_HANDLE			ImageHandle,
  IN EFI_SYSTEM_TABLE	*SystemTable )
{
	EFI_STATUS status;
	UINTN handleCount;
	EFI_HANDLE *handleBuffer;
	EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *pciRootBridgeIo;

    status = gBS->LocateHandleBuffer(ByProtocol, &gEfiPciRootBridgeIoProtocolGuid, NULL, &handleCount, &handleBuffer);
    if (EFI_ERROR(status))
    {
    	FreePool(handleBuffer);
    	return status;
    }
	for (UINTN i = 0 ; i < handleCount ; i++)
	{
		status = gBS->OpenProtocol(handleBuffer[i], &gEfiPciRootBridgeIoProtocolGuid, (VOID **)&pciRootBridgeIo, gImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
		if (EFI_ERROR(status))
		{
			FreePool(handleBuffer);
			return status;
		}
		if (i == 0 || original_PciRootBridgeIo_Read == pciRootBridgeIo->Pci.Read)
		{
			original_PciRootBridgeIo_Read = pciRootBridgeIo->Pci.Read;  //replace the pciRootBridgeIo->Pci.Read Handler (It should the the same for all root bridges)
			pciRootBridgeIo->Pci.Read = &new_PciRootBridgeIo_Read;
		}
	}
	FreePool(handleBuffer);
	return EFI_SUCCESS;
}
