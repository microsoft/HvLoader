/*++

Copyright (c) Microsoft Corporation

Module Name:

    HvLoader.h

Abstract:

    Includes the types used by external hypervisor loader that is to be loaded
    by HvLoader.efi.

Environment:

    EDK2.

--*/

#ifndef HVLOADER_H
#define HVLOADER_H


//
// ---------------------------------------------------------------------- Types
//

//
// Loaded image information
//
typedef struct {
  //
  // Loaded image based address.
  //
  EFI_PHYSICAL_ADDRESS  ImageAddress;

  //
  // Loaded image size.
  //
  UINT64                ImageSize;

  //
  // Loaded image page count.
  //
  UINTN                 ImagePages;

  //
  // Loaded image memory type.
  //
  EFI_MEMORY_TYPE       ImageMemoryType;

  //
  // Loaded image entry point.
  //
  EFI_PHYSICAL_ADDRESS  EntryPoint;

} HVL_LOADED_IMAGE_INFO;


/**
  This is the external hypervisor loader image entry point.

  @param[in]  ImageHandle         The firmware allocated handle for the primary
                                  EFI loader.
  @param[in]  SystemTable         A pointer to the EFI System Table.
  @param[in]  HvLoaderImageInfo   A pointer to the loaded hypervisor loader 
                                  image information.

  @retval EFI_SUCCESS             The operation completed successfully.
  @retval Others                  An unexpected error occurred.
**/
typedef
EFI_STATUS
(EFIAPI *HV_LOADER_IMAGE_ENTRY_POINT) (
  IN  EFI_HANDLE                   ImageHandle,
  IN  EFI_SYSTEM_TABLE             *SystemTable,
  IN  HVL_LOADED_IMAGE_INFO        *HvLoaderImageInfo
  );

#endif // !HVLOADER_H