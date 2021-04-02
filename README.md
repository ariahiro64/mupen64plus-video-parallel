# mupen64plus-video-parallel

Implementation of Themaister's Vulkan RDP emulator over OGL 3.3.


**Disclaimer**

**Do not expect any support/help. Pull requests will be dismissed.**

Made after a few days of messing around with GCC and some misc. sources.

To compile using MSYS2 
1) Clone/fork the Github repo only.
2) At "unix" directory, use "make all platform=win"

Your choices for using this include: 
* the commandline client
* Rosalie's Mupen GUI 
* "m64p"
To use it with m64p (for Windows):

1) Compile using the steps above.
2) Rename the compiled DLL to "mupen64plus-video-angrylion-plus.dll".
3) Replace the old Angrylion Plus DLL (create a backup if you still need it)

TODO:
* Any possible Linux testing