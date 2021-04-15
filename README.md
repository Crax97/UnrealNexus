How to build:

1. Compile corto as a library:
   **ON LINUX: Make sure to compile the library against libc++**, more informations at https://libcxx.llvm.org/docs/UsingLibcxx.html (UE4 is linked to libc++ so you're kinda forced)
2. Copy the library file (it should be named corto.lib on Windows, corto.a on \*nix) to `UnrealNexus/Plugins/NexusPlugin/Source/NexusPlugin/libs/` (if the folder does not exist just create it)
3. Compile the project and have fun.
