/** @file
  Definitions related to LINUX_EFI_HYPERVISOR_MEDIA_GUID protocol exposed
  by the hypervisor loader, loaded by HvLoader.efi.

  Note !!!
    This is only used for testing the exposed protocol. It is not included 
    in a production build.

  Copyright (c) 2022, Microsoft Corporation.
  
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef __HVEFI_H__
#define __HVEFI_H__

//
// -------------------------------------------------------------------- Defines
//

//
// LINUX_EFI_HYPERVISOR_MEDIA protocol for launching Hyper-V
//

#define LINUX_EFI_HYPERVISOR_MEDIA_GUID \
        {0x098d423a, 0x6ca5, 0x4ad4, \
        {0x90, 0xfa, 0x72, 0xc3, 0xce, 0x22, 0xc8, 0xd0}}

//
// HV EFI memory descriptor extended attributes values
//

#define HV_EFI_MEMORY_EX_ATTR_HV        0x0000000000001 // Hypervisor pages
#define HV_EFI_MEMORY_EX_ATTR_HVLOADER  0x0000000000002 // HV loader pages


//
// ---------------------------------------------------------------------- Types
//

//
// HV EFI descriptor extention that includes extended attributes.
//

typedef struct _HV_EFI_MEMORY_DESCRIPTOR_EX {
    //
    // EFI_MEMORY_DESCRIPTOR aligned to 128 bits
    //
    // HV_EFI_MEMORY_DESCRIPTOR_EX:
    UINT64  ExAttribute;  //  Field size is 64 bits
    UINT64  Pad;          //  Field size is 64 bits
} HV_EFI_MEMORY_DESCRIPTOR_EX, *PHV_EFI_MEMORY_DESCRIPTOR_EX;

//
// LINUX_EFI_HYPERVISOR_MEDIA_PROTOCOL methods
//

typedef
VOID
(EFIAPI *HV_EFI_LAUNCH_HYPERVISOR_ROUTINE) (
    IN OPTIONAL VOID *SanitizeBspContext,
    OUT         VOID *HvlReturnData
    );

typedef
UINT32
(EFIAPI *HV_EFI_REGISTER_RUNTIME_RANGE_ROUTINE) (
    IN UINT64 BasePage,
    IN UINT64 PageCount
    );

typedef
EFI_STATUS
(EFIAPI *HV_EFI_GET_MEMORY_MAP_ROUTINE) (
    IN OUT  UINTN                   *EfiMemoryMapSize,
    IN OUT  EFI_MEMORY_DESCRIPTOR   *EfiMemoryMap,
    OUT     UINTN                   *MapKey,
    OUT     UINTN                   *DescriptorSize,
    OUT     UINT32                  *DescriptorVersion
    );

typedef
CHAR16*
(EFIAPI *HV_EFI_GET_NEXT_LOG_MESSAGE_ROUTINE) (
    IN OUT  size_t *NextMessage
    );

typedef struct _LINUX_EFI_HYPERVISOR_MEDIA_PROTOCOL {
    HV_EFI_LAUNCH_HYPERVISOR_ROUTINE      HvlLaunchHv;
    HV_EFI_REGISTER_RUNTIME_RANGE_ROUTINE HvlRegisterRuntimeRange;
    HV_EFI_GET_MEMORY_MAP_ROUTINE         HvlGetMemoryMap;
    HV_EFI_GET_NEXT_LOG_MESSAGE_ROUTINE   HvlGetNextLogMessage;
} LINUX_EFI_HYPERVISOR_MEDIA_PROTOCOL;

#endif // !__HVEFI_H__