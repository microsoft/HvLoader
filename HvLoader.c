/** @file
  This is a shell application that loads a hypervisor loader binary, and 
  then calls into its entry point to load the hypervisor without running it.

  Copyright (c) 2022, Microsoft Corporation.
  
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

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


// -------------------------------------------------------------------- Defines

//
// Default HV loader DLL path
//
#define DEF_HVLOADER_DLL_PATH L"\\Windows\\System32\\lxhvloader.dll"

//
// SHIM LOCK protocol GUID
//
#define EFI_SHIM_LOCK_GUID \
        {0x605dab50, 0xe046, 0x4300, \
        {0xab, 0xb6, 0x3d, 0xd8, 0x10, 0xdd, 0x8b, 0x23 }}


// ---------------------------------------------------------------------- Types

//
// EFI_SHIM_LOCK_GUID_PROTOCOL
//

typedef
EFI_STATUS
(*EFI_SHIM_LOCK_VERIFY) (
    VOID    *Buffer,
    UINT32  Size
    );

typedef struct  {
    EFI_SHIM_LOCK_VERIFY  Verify;
    VOID                  *Hash;
    VOID                  *Context;
} EFI_SHIM_LOCK_GUID_PROTOCOL;


// -------------------------------------------------------------------- Globals

EFI_GUID gEfiShimLockProtocolGuid = EFI_SHIM_LOCK_GUID;


// ------------------------------------------------------------------ Functions

/**
  Gets the hypervisor loader binary (DLL) file path from command line.
  The DLL path should be the first command line option! 

  @param[in]  LoadedImage           The EFI_LOADED_IMAGE_PROTOCOL interface for 
                                    this app.
  @param[out]  HvLoaderDllPath      The return address of hypervisor loader 
                                    DLL path.

  @return EFI_SUCCESS    If DLL path was acquired successfully. 
  @return Others         Invalid args or out of resources. 
**/
EFI_STATUS
HvlGetHvLoaderDllPath (
  IN  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage,
  OUT CHAR16*                   *HvLoaderDllPath
  )
{

  UINT32  MaxPathSize;
  CHAR16* Path;
  UINT32  PathSize;

  if ((LoadedImage == NULL) || (HvLoaderDllPath == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // If command line is empty, use the default path, otherwise use the first
  // command line option as the loader DLL path.
  //

  if (LoadedImage->LoadOptionsSize == 0) {
    Path = DEF_HVLOADER_DLL_PATH;
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

  @return EFI_SUCCESS    If DLL file was successfully read to memory buffer.
  @return Others         If DLL file was not found, or we ran out of resources.
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

  @return EFI_SUCCESS   File content is verified, and TPM PCRs are extended
                        file's has.
  @return Others        Otherwise.
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

  @param[in]  PeCoffImage    Point to a Pe/Coff image.
  @param[out] ImageAddress   The image memory address after relocation.
  @param[out] ImageSize      The image size.
  @param[out] ImagePages     The image pages.
  @param[out] EntryPoint     The image entry point.

  @return EFI_SUCCESS    If the image is loaded and relocated successfully.
  @return Others         If the image failed to load or relocate.
**/
EFI_STATUS
HvlLoadPeCoffImage (
  IN  VOID                  *PeCoffImage,
  OUT EFI_PHYSICAL_ADDRESS  *ImageAddress,
  OUT UINT64                *ImageSize,
  OUT UINTN                 *ImagePages,
  OUT EFI_PHYSICAL_ADDRESS  *EntryPoint
  )
{

  PHYSICAL_ADDRESS              ImageBuffer;
  PE_COFF_LOADER_IMAGE_CONTEXT  ImageContext;
  EFI_STATUS                    Status;

  ZeroMem(&ImageContext, sizeof (ImageContext));
  ImageContext.Handle    = PeCoffImage;
  ImageContext.ImageRead = PeCoffLoaderImageReadFromMemory;

  ImageBuffer = 0;

  Status = PeCoffLoaderGetImageInfo(&ImageContext);
  if (EFI_ERROR (Status)) {
    Print(L"Error: PeCoffLoaderGetImageInfo failed, status %d!\r\n", Status);
    goto Done;
  }

  /*
   * Allocate Memory for the image.
   * We use memory type of EfiRuntimeServicesCode since we need it to persist
   * after 'Exit Boot Services'. HV loader DLL can mark these pages as 
   * EfiConventionalMemory so the guest kernel can reclaim those.
   */

  *ImagePages = EFI_SIZE_TO_PAGES(ImageContext.ImageSize);

  Status = gBS->AllocatePages (
                  AllocateAnyPages,
                  EfiRuntimeServicesCode,
                  *ImagePages,
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

  *ImageAddress = ImageContext.ImageAddress;
  *ImageSize    = ImageContext.ImageSize;
  *EntryPoint   = ImageContext.EntryPoint;

  Status = EFI_SUCCESS;

Done:

  if (EFI_ERROR(Status)) {
    if (ImageBuffer != 0) {
      gBS->FreePages(ImageBuffer, *ImagePages);
    }
  }

  return Status;
}


/**
  HvLoader.efi application entry point.

  HvLoader.efi securely loads an external hypervisor loader, and calls its 
  entrypoint.
  The external loader entrypoint is assumed to be of EFI_IMAGE_ENTRY_POINT 
  type, and HvLoader.efi passes it's own ImageHandle, so the loader has access
  to HvLoder.efi's command line options, provided by the boot loader.

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
  EFI_PHYSICAL_ADDRESS      DllImageBaseAddress;
  EFI_PHYSICAL_ADDRESS      DllImageEntryPoint;
  UINTN                     DllImagePages;
  UINT64                    DllImageSize;
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
  EFI_STATUS                Status;

  Print(L"Hvloader.efi starting...\r\n");
  
  DllFileBuffer       = NULL;
  DllFilePath         = NULL;
  DllImageBaseAddress = 0;

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

  Status = HvlGetHvLoaderDllPath(LoadedImage, &DllFilePath);
  if (EFI_ERROR(Status)) {
    Print(L"Error: Failed to get DLL path, status %d!\r\n", Status);
    goto Done;
  }    

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

  if (Status == EFI_NOT_FOUND) {
    Status = HvlLoadLoaderDll(
                LoadedImage, 
                DEF_HVLOADER_DLL_PATH, 
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

  Status = HvlLoadPeCoffImage(
              DllFileBuffer,
              &DllImageBaseAddress,
              &DllImageSize,
              &DllImagePages,
              &DllImageEntryPoint
              );

  if (EFI_ERROR(Status)) {
    Print(L"Error: Failed to load PE/COFF image, status %d!\r\n", Status);
    goto Done;
  }

  //
  // Run HV loader using our image handle.
  //

  Status = ((EFI_IMAGE_ENTRY_POINT)DllImageEntryPoint)(
                                      ImageHandle, 
                                      SystemTable
                                      );

  if (EFI_ERROR(Status)) {
    Print(L"Error: HV loader failed, status %d!\r\n", Status);
    goto Done;
  }

  Print(L"Hvloader: completed successfully\r\n");

  Status = EFI_SUCCESS;

Done:

  //
  // Failure cleanup
  //

  if (Status != EFI_SUCCESS) {
    if (DllImageBaseAddress != 0) {
      gBS->FreePages(DllImageBaseAddress, DllImagePages);
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