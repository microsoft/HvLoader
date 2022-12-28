# HvLoader Overview
HvLoader.efi is an EFI application for loading an external hypervisor loader.

HvLoader.efi loads a given hypervisor loader binary (DLL, EFI, etc.), and calls
it's entry point passing HvLoader.efi ImageHandle. This way the hypervisor loader 
binary has access to HvLoader.efi's command line options, and use those as 
configuration parameters. The first HvLoader.efi command line option is the 
path to hypervisor loader binary.

Currently HvLoader.efi resides in the efi partition, i.e. /boot/efi, and has
access to that partition only. Thus, any path arguments need to be relative to
/boot/efi partition. This restriction may be relaxed in future updates.

A typical HvLoader.efi grub command may look like the following:

_chainloader /HvLoader.efi \\Windows\\System32\\lxhvloader.dll MSHV_ROOT=\\Windows MSHV_ENABLE=1 MSHV_SCHEDULER_TYPE=0 ..._

## Setting up the build environment
HvLoader.efi is built in [TianoCore EDK2](https://github.com/tianocore/edk2).
The build environment was initially tested on a Ubuntu 22.04.1 LTS, but can be
set up on Microsoft Windows, macOS, and Unix.  
For more information, please refer to [Getting Started with EDK2](https://github.com/tianocore/tianocore.github.io/wiki/Getting-Started-with-EDK-II).

On Linux, please follow the instructions at [Using EDK2 with Native GCC](https://github.com/tianocore/tianocore.github.io/wiki/Using-EDK-II-with-Native-GCC), and
finally clone the repo and build according to [Common EDK II Build Instructions for Linux](https://github.com/tianocore/tianocore.github.io/wiki/Common-instructions).

## Building HvLoader.efi
HvLoader.efi lives as an application in MdeModulePkg, i.e. <edk2>/MdeModulePkg/Application/HvLoader.

After a successful build of EDK2, you can build HvLoader.efi following the steps below,   
running from the root location of edk2:
1. _pushd MdeModulePkg/Application_
2. _git clone https://github.com/asherkariv/HvLoader.git_
3. _popd_
4. Add HvLoader to MdeModulePkg:
   * Edit _MdeModulePkg/MdeModulePkg.dsc_
   * Add _MdeModulePkg/Application/HvLoader/HvLoader.inf_ in the [Components] section, for example:        
     **[Components]**   
        MdeModulePkg/Application/HelloWorld/HelloWorld.inf
        **MdeModulePkg/Application/HvLoader/HvLoader.inf**
        MdeModulePkg/Application/DumpDynPcd/DumpDynPcd.inf
        MdeModulePkg/Application/MemoryProfileInfo/MemoryProfileInfo.inf
        ...

5. Build HvLoder.efi:   
   _build -p MdeModulePkg/MdeModulePkg.dsc -m MdeModulePkg/Application/HvLoader/HvLoader.inf_
6. Based on your target configuration (DEBUG/RELEASE), HvLoader.efi is produced at 
   _Build/MdeModule/DEBUG_GCC5/X64/HvLoader.efi_   
   or   
   _Build/MdeModule/RELEASE_GCC5/X64/HvLoader.efi_   

## Signing HvLoader.efi
HvLoader.efi needs to be signed for secure boot.




