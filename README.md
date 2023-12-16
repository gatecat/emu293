# SPG293 Console Emulator -- alpha

This is an emulator for a range of obscure clone consoles using the Sunplus SPG293 SoC.

Currently the target platform is a Lexibook JG7425 although Subor systems (who seem to be the OEM for the Lexibook one too) are also being investigated.

Building (if not using a prebuilt package):

 - Install SDL and WxWidgets
 - run `make` in `src/`

Getting Started:
 - See the READMEs in the roms/ folder for further information.
 - Fetch the lx_aven or jg7425 CHD from your favourite source of MAME romsets
 - Convert it to a plain SD card image with `chdman extracthd -i sd-card.chd -f -o sd_card.img`
 - Either, fetch the NOR flash image too (file inside `lx_jg7425.zip` or `lx_aven.zip`) or mount the SD card image and copy the file `windows/Lead.sys` somewhere useful - this is the boot application.
 - run `emu293.exe` or `emu293` and select a system from the GUI.
 - alternatively, on the command line run `./emu293 Lead.sys sd_card.img` or `./emu293 -nor mx29lv160.u6 sd_card.img`

Controls:

player 1
 - arrow keys: joystick (in horizontal orientation, so wrongly rotated in menu)
 - enter: start
 - rshift: select
 - x, z: B, A
 - c: "motion"

player 2:
 - \[: start
 - \]: select
 - a, s: B, A
 - d: "motion"

system:
 - F9: soft reset
 - alt+F4: quit
 - alt+{1-9}: save state to slot 1-9
 - ctrl+{1-9}: load state from slot 1-9

(only enough player 2 controls are implemented for a few motion games that use both remotes).

Known issues:
 - Several games have some graphical bugs
 - More advanced wavetable SPU functionality is imperfectly emulated
 - Vsync/timer frequencies are inaccurate

Subor A21 support (even more experimental, Linux only):
 - Download the Subor A21 SD card RAR from [archive.org](https://archive.org/details/a-21_20230131)
 - Copy it into a SD card image with a single FAT32 partition, cp936 charset is probably required
 - Save the rom.elf in the root folder separately
 - Run emu293 as `./emu293 -cam /dev/video0 rom.elf sd_card.img` (or whatever V4L device you want to use for the emulated camera)

Zone 3D support:
 - Fetch the zone3d CHD and NOR flash image (`zone_25l8006e_c22014.bin` from `zone3d.zip`) from your favourite source of MAME romsets
 - Convert it to a plain SD card image with `chdman extracthd -i sd-card.chd -f -o sd_card.img`
 - Select the Zone 3D in the GUI or on the commandun emu293 as `./emu293 -zone3d -nor zone_25l8006e_c22014.bin sd_card.img` 

Credit to LiraNura's [hyperscan-emulator](https://github.com/LiraNuna/hyperscan-emulator/) from which the S+Core CPU core in here is currently based on.
