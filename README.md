# This is still a WIP!

How to build:

1. Compile corto as a library:
   **ON LINUX: Make sure to compile the library against libc++**, more informations at https://libcxx.llvm.org/docs/UsingLibcxx.html (UE4 is linked to libc++ so you're kinda forced)
2. Copy the library file (it should be named corto.lib on Windows, corto.a on \*nix) to `UnrealNexus/Plugins/NexusPlugin/Source/NexusPlugin/libs/` (if the folder does not exist just create it)
3. Compile the project and have fun.

### Ok but how do i use it?
Add the Nexus/Plugins/NexusPlugin folder to your Plugins folder

Download a huge model (e.g a 3D scan) and convert it to a nxz file using [Nexus](https://github.com/cnr-isti-vclab/nexus)'s utilities.

Import it into the project (make sure to import it into a separate folder, since each node's data has it's own asset).

Add a UUnrealNexusComponent to your actor, select the Nexus asset and when you play the model should start being rendered using Nexus (if it doesn't feel free to bonk me in the head)