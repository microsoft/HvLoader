/** @file
  Definitions used by privately by HvLoader.efi application.

  Copyright (c) 2022, Microsoft Corporation.
  
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef __HVLOADERP_H__
#define __HVLOADERP_H__

//
// -------------------------------------------------------------------- Defines
//

//
// HVL_TEST build. 
// Set to 1 to enable and use '--Test' command line option, for example:
// menuentry "With Hypervisor" {
//  ...
//  chainloader /HvLoader.efi --Test
//  boot
//  ...
// }
// 
//
#define HVL_TEST          0
#define HVL_TEST_VERBOSE  0

//
// Default HV loader DLL path.
//
#define HVL_DEF_LOADER_DLL_PATH   L"\\lxhvloader.dll"

//
// Default HV loader DLL path.
//
#define HVL_CMDLINE__TEST_RUN     L"--Test"

//
// Useful macros for setting and checking flags.
//
#define SET_FLAGS(_x, _f)         ((_x) |= (_f))
#define CLEAR_FLAGS(_x, _f)       ((_x) &= ~(_f))
#define CHECK_FLAG(_x, _f)        ((_x) & (_f))

//
// HV loader DLL path flags.
//
#define HVL_PATH_FLAG__DEF_PATH   0x00000001
#define HVL_PATH_FLAG__TEST_RUN   0x80000000

//
// The type of memory used for the hypervisor loader image.
// This memory can be reclaimed by the guest kernel, after the hypervisor has 
// started.
//
#define HVL_IMAGE_MEMORY_TYPE     EfiRuntimeServicesCode

//
// SHIM LOCK protocol GUID.
//
#define EFI_SHIM_LOCK_GUID \
        {0x605dab50, 0xe046, 0x4300, \
        {0xab, 0xb6, 0x3d, 0xd8, 0x10, 0xdd, 0x8b, 0x23 }}


//
// ---------------------------------------------------------------------- Types
//

//
// EFI_SHIM_LOCK_GUID_PROTOCOL
//

/**
  This is the EFI_SHIM_LOCK_GUID_PROTOCOL content verify method..

  @param[in]  Buffer    The memory buffer containing the data to be verified. 
  @param[in]  Size      Size of data to be verified.

  @return EFI_SUCCESS   File content is verified, and TPM PCRs are extended
                        file's hash.
  @return Others        Otherwise.
**/
typedef
EFI_STATUS
(*EFI_SHIM_LOCK_VERIFY) (
    IN VOID    *Buffer,
    IN UINT32  Size
    );

//
// EFI_SHIM_LOCK_GUID_PROTOCOL protocol interface
//
typedef struct  {
    EFI_SHIM_LOCK_VERIFY  Verify;
    VOID                  *Hash;
    VOID                  *Context;
} EFI_SHIM_LOCK_GUID_PROTOCOL;


//
// -------------------------------------------------------------------- Globals
//

extern EFI_GUID gEfiShimLockProtocolGuid;


//
// ------------------------------------------------------------------ FUnctions
//

VOID
HvlTestRun (
  VOID
  );

#endif // !__HVLOADERP_H__