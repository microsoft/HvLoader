/** @file
  This is a shell application that loads a hypervisor loader binary, and 
  then calls into its entry point to load the hypervisor without running it.

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

#include "HvLoader.h"
#include "HvLoaderP.h"


//
// -------------------------------------------------------------------- Globals
//

EFI_GUID gEfiShimLockProtocolGuid = EFI_SHIM_LOCK_GUID;

//
// ------------------------------------------------------------------ Functions
//


/**
  Gets the hypervisor loader binary (DLL) file path from command line.
  The DLL path should be the first command line option! 

  @param[in]  LoadedImage     The EFI_LOADED_IMAGE_PROTOCOL interface for 
                              this app.
  @param[out] HvLoaderDllPath The return address of hypervisor loader
                              DLL path.
  @param[out] Flags           The return address of hypervisor loader
                              DLL path flags.

  @return EFI_SUCCESS         If DLL path was acquired successfully. 
  @return Others              Invalid args or out of resources. 
**/
EFI_STATUS
HvlGetHvLoaderDllPath (
  IN  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage,
  OUT CHAR16*                   *HvLoaderDllPath,
  OUT UINT32                    *Flags
  )
{

  UINT32  MaxPathSize;
  CHAR16* Path;
  UINT32  PathSize;

  *Flags = 0;

  if ((LoadedImage == NULL) || (HvLoaderDllPath == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // If command line is empty, use the default path, otherwise use the first
  // command line option as the loader DLL path.
  //

  if (LoadedImage->LoadOptionsSize == 0) {
    Path = HVL_DEF_LOADER_DLL_PATH;
    PathSize = StrLen(Path);
  } else {
    MaxPathSize = LoadedImage->LoadOptionsSize / sizeof(CHAR16);
    Path = LoadedImage->LoadOptions;
    PathSize = 0;

    while (PathSize < MaxPathSize) {
      if (Path[PathSize] == (CHAR16)' ') {
        break;
      }

      PathSize++;
    } 
  }

  PathSize *= sizeof(CHAR16);

  *HvLoaderDllPath = AllocateZeroPool(PathSize + sizeof(CHAR16));
  if (*HvLoaderDllPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  CopyMem(*HvLoaderDllPath, Path, PathSize);

  if (!StrCmp(*HvLoaderDllPath, HVL_DEF_LOADER_DLL_PATH)) {
    SET_FLAGS(*Flags, HVL_PATH_FLAG__DEF_PATH);
  } else if (!StrCmp(*HvLoaderDllPath, HVL_CMDLINE__TEST_RUN)) {
    SET_FLAGS(*Flags, HVL_PATH_FLAG__TEST_RUN);
  }

  return EFI_SUCCESS;
}


/**
  Get file size.

  @param[in]  FileHandle Handle of the opened file.
  @param[out] FileSize   Address of returned file size.

  @return EFI_SUCCESS    If file size was successfully acquired.
  @return Others
**/
EFI_STATUS
HvlGetFileSize (
  IN  EFI_FILE_HANDLE DllFileHandle,
  OUT UINTN           *DllFileSize
  )
{

  UINTN         BufferSize;
  EFI_FILE_INFO *DllFileInfo;
  EFI_STATUS    Status;

  DllFileInfo = NULL;

  BufferSize = 0;
  Status = DllFileHandle->GetInfo(
                            DllFileHandle, 
                            &gEfiFileInfoGuid, 
                            &BufferSize, 
                            NULL
                            );

  if (Status != EFI_BUFFER_TOO_SMALL) {
    Print(
      L"Error: Unexpected getting file information status %d, expected %s!\r\n", 
      Status,
      EFI_BUFFER_TOO_SMALL
      );

    goto Done;
  }

  DllFileInfo = AllocateZeroPool(BufferSize);
  if (DllFileInfo == NULL) {
    Print(
      L"Error: Failed to allocated %d bytes, for DLL file information!\r\n",
      BufferSize
      );

    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }

  Status = DllFileHandle->GetInfo(
                            DllFileHandle, 
                            &gEfiFileInfoGuid, 
                            &BufferSize, 
                            DllFileInfo
                            );

  if (EFI_ERROR(Status)) {
    Print(
      L"Error: Failed to get DLL file information, status %d!\r\n", 
      Status
      );

    goto Done;
  }

  *DllFileSize = DllFileInfo->FileSize;

  Status = EFI_SUCCESS;

Done:

  if (DllFileInfo != NULL) {
    FreePool(DllFileInfo);
  }

  return Status;
}


/**
  Reads HV loader dll file to memory.

  @param[in]  LoadedImage   The EFI_LOADED_IMAGE_PROTOCOL interface for 
                            this app.
  @param[in]  DllFilePath   The hypervisor loader DLL file path.
  @param[out] DllFileBuffer Address of returned HV loader DLL buffer.
  @param[out] DllFileSize   Address of returned HV loader DLL buffer size.

  @return EFI_SUCCESS       If DLL file was successfully read to memory buffer.
  @return Others            If DLL file was not found, or we ran out of 
                            resources.
**/
EFI_STATUS
HvlLoadLoaderDll (
  IN  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage,
  IN  CHAR16                    *DllFilePath,
  OUT VOID*                     *DllFileBuffer,
  OUT UINTN                     *DllFileSize
  )
{

  EFI_FILE_HANDLE                 DllFileHandle;
  EFI_FILE_HANDLE                 FsRoot;
  EFI_STATUS                      Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Vol;

  DllFileHandle = NULL;
  FsRoot = NULL;

  //
  // Get the volume where hvloader.efi resides.
  //

  Status = gBS->HandleProtocol(
                  LoadedImage->DeviceHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  (VOID **)&Vol
                  );

  if (EFI_ERROR(Status)) {
    Print(
      L"Error: Opening EfiSimpleFileSystemProtocolGuid failed, "
      L"status 0x%X!\r\n", 
      Status
      );

    goto Done;
  }

  //
  // Get volume root.
  //

  Status = Vol->OpenVolume(Vol, &FsRoot);
  if (EFI_ERROR(Status)) {
    Print(L"Error: Opening FS root failed, status %d!\r\n", Status);
    goto Done;
  }

  //
  // Open the loader DLL file. Path is relative to efi partition.
  //

  Status = FsRoot->Open(
                    FsRoot, 
                    &DllFileHandle, 
                    DllFilePath, 
                    EFI_FILE_MODE_READ, 
                    EFI_FILE_READ_ONLY
                    );

  if (EFI_ERROR(Status)) {
    Print(
      L"Error: Failed to open DLL file %s, status %d!\r\n", 
      DllFilePath, 
      Status
      );

    goto Done;
  }

  //
  // Get DLL file size information.
  //

  Status = HvlGetFileSize(DllFileHandle, DllFileSize);
  if (EFI_ERROR(Status)) {
    Print(
      L"Error: Failed to get DLL file information, status %d!\r\n", 
      Status
      );

    goto Done;
  }

  //
  // Allocate a buffer and read the DLL file to memory.
  //

  *DllFileBuffer = AllocateZeroPool(*DllFileSize);
  if (*DllFileBuffer == NULL) {
    Print(
      L"Error: Failed to allocate DLL file buffer, size %d!\r\n", 
      DllFileSize
      );

    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }

  Status = DllFileHandle->Read(DllFileHandle, DllFileSize, *DllFileBuffer);
  if (EFI_ERROR(Status)) {
    Print(
      L"Error: Failed to read DLL file, status %d size %d!\r\n", 
      Status, 
      DllFileSize
      );

    goto Done;
  }

  Status = EFI_SUCCESS;

Done:

  if (DllFileHandle != NULL) {
    DllFileHandle->Close(DllFileHandle);
  }

  if(FsRoot != NULL) {
    FsRoot->Close(FsRoot);
  }

  return Status;
}


/**
  Use EFI_SHIM_LOCK_GUID_PROTOCOL to verify a memory buffer.
  This call verifies the content is correctly signed and extends the TPM PCRs 
  with the content hash.

  In case of a binary file, input buffer should contain the file as read from 
  disk, before any processing is applied, like image relocations, etc.

  @param[in]  Contet      File content to be verified.
  @param[in]  ContetSize  File content (bytes).

  @return EFI_SUCCESS     File content is verified, and TPM PCRs are extended
                          file's hash.
  @return Others          Otherwise.
**/
EFI_STATUS
HvlShimVerify (
  IN  VOID    *Contet,
  IN  UINT32  ContetSize
  )
{

  EFI_SHIM_LOCK_GUID_PROTOCOL *ShimLock;
  EFI_STATUS                  Status;

  Status = gBS->LocateProtocol(
                  &gEfiShimLockProtocolGuid,
                  NULL,
                  (VOID **)&ShimLock
                  );

  if (EFI_ERROR(Status)) {
    Print(L"Error: Failed to locate SHIM_LOCK protocol, status %d!\r\n", Status);
    return Status;
  }

  Status = ShimLock->Verify(Contet, ContetSize);
  if (EFI_ERROR(Status)) {
    Print(L"Error: SHIM_LOCK verification failed, status %d!\r\n", Status);
    return Status;
  }

  return EFI_SUCCESS;
}


/**
  Loads and relocates a PE/COFF image.

  @param[in]  PeCoffImage     Point to a Pe/Coff image.
  @param[out] LoadedImageInfo The loaded image information.

  @return EFI_SUCCESS         If the image is loaded and relocated 
                              successfully.
  @return Others              If the image failed to load or relocate.
**/
EFI_STATUS
HvlLoadPeCoffImage (
  IN  VOID                  *PeCoffImage,
  OUT HVL_LOADED_IMAGE_INFO *LoadedImageInfo
  )
{

  PHYSICAL_ADDRESS              ImageBuffer;
  PE_COFF_LOADER_IMAGE_CONTEXT  ImageContext;
  UINTN                         ImagePages;
  EFI_STATUS                    Status;

  ZeroMem(&ImageContext, sizeof(ImageContext));
  ImageContext.Handle    = PeCoffImage;
  ImageContext.ImageRead = PeCoffLoaderImageReadFromMemory;

  ImageBuffer = 0;
  ImagePages  = 0;

  Status = PeCoffLoaderGetImageInfo(&ImageContext);
  if (EFI_ERROR (Status)) {
    Print(L"Error: PeCoffLoaderGetImageInfo failed, status %d!\r\n", Status);
    goto Done;
  }

  /*
   * Allocate Memory for the image.
   * We use memory type of HVL_IMAGE_MEMORY_TYPE, and save it in the loaded
   * image information. HV loader DLL can mark these pages as 
   * EfiConventionalMemory so the guest kernel can reclaim those.
   */

  ImagePages = EFI_SIZE_TO_PAGES(ImageContext.ImageSize);

  Status = gBS->AllocatePages (
                  AllocateAnyPages,
                  HVL_IMAGE_MEMORY_TYPE,
                  ImagePages,
                  &ImageBuffer
                  );

  if (EFI_ERROR (Status)) {
    Print(L"Error: AllocatePages failed, status %d!\r\n", Status);
    goto Done;
  }

  ImageContext.ImageAddress = ImageBuffer;

  /*
   * Load the image to our new buffer.
   */

  Status = PeCoffLoaderLoadImage(&ImageContext);
  if (EFI_ERROR (Status)) {
    Print(L"Error: PeCoffLoaderLoadImage failed, status %d!\r\n", Status);
    goto Done;
  }

  //
  // Relocate the image in our new buffer.
  //

  Status = PeCoffLoaderRelocateImage(&ImageContext);
  if (EFI_ERROR (Status)) {
    Print(L"Error: PeCoffLoaderRelocateImage failed, status %d!\r\n", Status);
    goto Done;
  }

  LoadedImageInfo->ImageAddress     = ImageContext.ImageAddress;
  LoadedImageInfo->ImageSize        = ImageContext.ImageSize;
  LoadedImageInfo->ImagePages       = ImagePages;
  LoadedImageInfo->ImageMemoryType  = HVL_IMAGE_MEMORY_TYPE;
  LoadedImageInfo->EntryPoint       = ImageContext.EntryPoint;

  Status = EFI_SUCCESS;

Done:

  if (EFI_ERROR(Status)) {
    if (ImageBuffer != 0) {
      gBS->FreePages(ImageBuffer, ImagePages);
    }
  }

  return Status;
}

/**
  HvLoader.efi application entry point.

  HvLoader.efi securely loads an external hypervisor loader, and calls its 
  entrypoint.
  The external loader entrypoint is assumed to be of 
  HV_LOADER_IMAGE_ENTRY_POINT type, and HvLoader.efi passes it's own 
  ImageHandle, so the loader has access to HvLoder.efi's command line options, 
  provided by the boot loader.

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.
  @param[in] SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS       The entry point is executed successfully.
  @retval other             Some error occurs when executing this entry point.

**/
EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  CHAR16                    *DllFilePath;
  VOID                      *DllFileBuffer;
  UINTN                     DllFileSize;
  UINT32                    DllPathFlags;
  HVL_LOADED_IMAGE_INFO     DllImageInfo;
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
  EFI_STATUS                Status;

  Print(L"Hvloader.efi starting...\r\n");
  
  DllFileBuffer       = NULL;
  DllFilePath         = NULL;
  DllPathFlags        = 0;
  ZeroMem(&DllImageInfo, sizeof(DllImageInfo));

  //
  // Get access to loader app command line, device path, etc.
  //

  Status = gBS->HandleProtocol(
                  ImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&LoadedImage
                  );

  if (EFI_ERROR(Status)) {
    Print(L"Error: Failed to access loader app information, status %d!\r\n", 
      Status);

    goto Done;
  }

  //
  // Get HV loader DLL path.
  //

  Status = HvlGetHvLoaderDllPath(LoadedImage, &DllFilePath, &DllPathFlags);
  if (EFI_ERROR(Status)) {
    Print(L"Error: Failed to get DLL path, status %d!\r\n", Status);
    goto Done;
  }    

#if HVL_TEST
  if (CHECK_FLAG(DllPathFlags, HVL_PATH_FLAG__TEST_RUN)) {
    HvlTestRun();
    goto Done;
  }
#endif // HVL_TEST

  //
  // Read HV loader DLL file to memory.
  //

  Status = HvlLoadLoaderDll(
              LoadedImage, 
              DllFilePath, 
              &DllFileBuffer, 
              &DllFileSize
              );

  //
  // If the given DLL file was not found, try the default path.
  //

  if ((Status == EFI_NOT_FOUND) &&
      (!CHECK_FLAG(DllPathFlags, HVL_PATH_FLAG__DEF_PATH))) {

        Status = HvlLoadLoaderDll(
                    LoadedImage, 
                    HVL_DEF_LOADER_DLL_PATH, 
                    &DllFileBuffer, 
                    &DllFileSize
                    );
  }

  if (EFI_ERROR(Status)) {
    Print(L"Error: Failed to load DLL file to memory, status %d!\r\n", Status);
    goto Done;
  }    

  //
  // Verify the file is correctly signed, and extend the TPM PCRs with 
  // file's hash.
  //

  Status = HvlShimVerify(DllFileBuffer, DllFileSize);
  if (EFI_ERROR(Status)) {
    Print(L"Error: DLL file verification failed, status %d!\r\n", Status);
    goto Done;
  }    

  //
  // Load HV loader DLL (PE/COFF) image from buffer.
  //

  Status = HvlLoadPeCoffImage(DllFileBuffer, &DllImageInfo);
  if (EFI_ERROR(Status)) {
    Print(L"Error: Failed to load PE/COFF image, status %d!\r\n", Status);
    goto Done;
  }

  //
  // Call the hypervisor loader entrypoint to load the hypervisor
  // and register the hypervisor protocol to be used by the guest kernel.
  //

  Status = ((HV_LOADER_IMAGE_ENTRY_POINT)DllImageInfo.EntryPoint)(
                                            ImageHandle, 
                                            SystemTable,
                                            &DllImageInfo
                                            );

  if (EFI_ERROR(Status)) {
    Print(L"Error: HV loader failed, status %d!\r\n", Status);
    goto Done;
  }

  Status = EFI_SUCCESS;

Done:

  //
  // Failure cleanup
  //

  if (Status != EFI_SUCCESS) {
    if (DllImageInfo.ImageAddress != 0) {
      gBS->FreePages(DllImageInfo.ImageAddress, DllImageInfo.ImagePages);
    }
  }

  //
  // General cleanup
  //

  if (DllFilePath != NULL) {
    FreePool(DllFilePath);
  }

  if (DllFileBuffer != NULL) {
    FreePool(DllFileBuffer);
  }
  
  return Status;
}