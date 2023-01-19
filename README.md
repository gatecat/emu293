# SPG293 Console Emulator -- alpha

***This is highly experimental and relatively awful codebase I mostly wrote 6-7 years ago while figuring all this stuff out. Please don't expect much.***

This is an emulator for a range of obscure clone consoles using the Sunplus SPG293 SoC.

Currently the target platform is a Lexibook JG7425 although Subor systems (who seem to be the OEM for the Lexibook one too) are also being investigated.

Building:

 - Install SDL etc
 - run `make` in `src/`

Using:

 - Fetch the lx_aven CHD from your favourite source of MAME romsets (JG7425 might work too, not yet tested).
 - Convert it to a plain SD card image with `chdman extracthd -i sd-card.chd -f -o sd_card.img`
 - Mount the SD card image somehow and copy the file `windows/Lead.sys` somewhere useful - this is the boot application.
 - run `./emu293 Lead.sys sd_card.img`

Controls:
 - arrow keys: joystick (in horizontal orientation, so wrongly rotated in menu)
 - enter: start
 - lshift: select
 - x, z: B, A

Known issues:
 - Several games have significant graphical bugs, tile layer scrolling is broken as is various advanced features
 - There's no SPU emulation at all
 - There are some interrupt-related crashes
 - It's too slow, and Vsync/timer frequencies are inaccurate

Credit to LiraNura's [hyperscan-emulator](https://github.com/LiraNuna/hyperscan-emulator/) from which the S+Core CPU core in here is currently based on.
