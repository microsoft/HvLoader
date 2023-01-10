/** @file
  This is a collection of units tests that can be used to verify hypervisor
  loader functionality. 
  
  This code is used for testing purposes only and is not part of production 
  images!

  Copyright (c) 2022, Microsoft Corporation.
  
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <stddef.h>
#include <Uefi.h>
#include <Library/PcdLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PeCoffLib.h>
#include <Library/PeCoffGetEntryPointLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/LoadFile.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/LoadPe32Image.h>
#include <Guid/FileInfo.h>

#include "HvLoaderEfi.h"
#include "HvLoaderP.h"

#if HVL_TEST
#include "HvEfi.h"


//
// -------------------------------------------------------------------- Defines
//

#define Add2Ptr(_ptr,_inc) ((VOID*)((CHAR8*)(_ptr) + (_inc)))


//
// -------------------------------------------------------------------- Globals
//

EFI_GUID gLinuxEfiHypervisorMediaGuid = LINUX_EFI_HYPERVISOR_MEDIA_GUID;

volatile int Busy = 1;

//
// ------------------------------------------------------------------ Functions
//


/**
  Run unit tests.

  @return None
**/
VOID
HvlTestRun (
  VOID
  )
{

    UINTN DescriptorSize;
    UINT32 DescriptorVersion;
    EFI_STATUS EfiStatus;
    EFI_MEMORY_DESCRIPTOR *EfiMemoryMap;
    UINTN EfiMemoryMapSize;
    LINUX_EFI_HYPERVISOR_MEDIA_PROTOCOL *HvEfiProtocol;
    HV_EFI_GET_MEMORY_MAP_ROUTINE HvlGetMemoryMap;
    UINTN MapKey;

    Print(L"\r\nHvloader.efi test run starting >>>\r\n");

    EfiMemoryMap  = NULL;

    EfiStatus = gBS->LocateProtocol(
                    &gLinuxEfiHypervisorMediaGuid,
                    NULL,
                    (VOID **)&HvEfiProtocol
                    );

    if (EFI_ERROR(EfiStatus)) {
        Print(L"Error: LocateProtocol failed, EFI status %d!\r\n", EfiStatus);
        goto Done;
    }

    //
    // Test LINUX_EFI_HYPERVISOR_MEDIA_PROTOCOL.HvlGetMemoryMap()
    //

    HvlGetMemoryMap = HvEfiProtocol->HvlGetMemoryMap;
    if (HvlGetMemoryMap == NULL) {
        Print(L"Error: Bad HV EFI protocol, no GetMemoryMap method!\r\n");
        EfiStatus = EFI_PROTOCOL_ERROR;
        goto Done;
    }

    EfiMemoryMapSize = 0;
    EfiStatus = HvlGetMemoryMap(
                  &EfiMemoryMapSize, 
                  EfiMemoryMap, 
                  &MapKey,
                  &DescriptorSize,
                  &DescriptorVersion
                  );
    
    if (EfiStatus != EFI_BUFFER_TOO_SMALL) {
        Print(
          L"Error: Unexpected EFI status %d, expected %d!\r\n", 
          EfiStatus, EFI_BUFFER_TOO_SMALL
          );

        goto Done;
    }

    Print(
      L"HvlpRunTests: Memory map size %d key 0x%X desc size %d "
      L"desc ver 0x%x\r\n", 
      EfiMemoryMapSize, MapKey, DescriptorSize, DescriptorVersion
      );

    EfiStatus = gBS->AllocatePool(
                      EfiBootServicesData, 
                      EfiMemoryMapSize, 
                      (VOID **)&EfiMemoryMap
                      );

    if (EFI_ERROR(EfiStatus)) {
        Print(L"Error: AllocatePool failed, status %d!\r\n", EfiStatus);
        goto Done;
    }

    ZeroMem(EfiMemoryMap, EfiMemoryMapSize);

    EfiStatus = HvlGetMemoryMap(
                  &EfiMemoryMapSize, 
                  EfiMemoryMap, 
                  &MapKey,
                  &DescriptorSize,
                  &DescriptorVersion
                  );
    
    if (EFI_ERROR(EfiStatus)) {
        Print(
          L"Error: HvlGetMemoryMap failed, status %d, required size %d !\r\n",
          EfiStatus, EfiMemoryMapSize
          );

        goto Done;
    }

    Print(
      L"HvlpRunTests: Allocated memory map size %d key 0x%X "
      L"desc size %d desc ver 0x%x\r\n",
      EfiMemoryMapSize, MapKey, DescriptorSize, DescriptorVersion
      );

    //
    // Print memory descriptor information.
    //
    {
        EFI_MEMORY_DESCRIPTOR *Descriptor;
        HV_EFI_MEMORY_DESCRIPTOR_EX *DescriptorEx;
        int Index;
        VOID *TableEnd;

        //
        // Printout memory descriptors
        //

        Descriptor = EfiMemoryMap;
        TableEnd = Add2Ptr(EfiMemoryMap, EfiMemoryMapSize);
        Index = 1;
        while (Descriptor != TableEnd) {
          DescriptorEx = Add2Ptr(
                          Descriptor, 
                          DescriptorSize - sizeof(*DescriptorEx)
                          );

#if HVL_TEST_VERBOSE
          Print(
            L"%02d) type 0x%X addr %p, np %d attr 0x%x xattr 0x%p\r\n",
            Index,
            Descriptor->Type,
            Descriptor->PhysicalStart,
            Descriptor->NumberOfPages,
            Descriptor->Attribute,
            DescriptorEx->ExAttribute
            );
#else // HVL_TEST_VERBOSE
          if (CHECK_FLAG(
                DescriptorEx->ExAttribute, 
                HV_EFI_MEMORY_EX_ATTR_HVLOADER
                )) {

            Print(
              L"Loader mem: type 0x%X addr %p, np %d attr 0x%x xattr 0x%p\r\n",
              Descriptor->Type,
              Descriptor->PhysicalStart,
              Descriptor->NumberOfPages,
              Descriptor->Attribute,
              DescriptorEx->ExAttribute
              );
          }

          if (CHECK_FLAG(
                DescriptorEx->ExAttribute, 
                HV_EFI_MEMORY_EX_ATTR_HV
                )) {

            Print(
              L"HV mem: type 0x%X addr %p, np %d attr 0x%x xattr 0x%p\r\n",
              Descriptor->Type,
              Descriptor->PhysicalStart,
              Descriptor->NumberOfPages,
              Descriptor->Attribute,
              DescriptorEx->ExAttribute
              );
          }
#endif // !HVL_TEST_VERBOSE
         Descriptor = Add2Ptr(Descriptor, DescriptorSize);
         Index++;
        }
    }

Done:

    Print(L"Hvloader.efi test run completed, status %d <<<\r\n", EfiStatus);

    if (EfiMemoryMap != NULL) {
        gBS->FreePool(EfiMemoryMap);
    }

    while (Busy) {}
}

#endif // HVL_TEST