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
 - Several games have significant graphical bugs
 - SPU emulation only supports basic modes playing \[AD\]PCM samples from memory; CPU driven modes are unsupported
 - It's too slow, and Vsync/timer frequencies are inaccurate

Credit to LiraNura's [hyperscan-emulator](https://github.com/LiraNuna/hyperscan-emulator/) from which the S+Core CPU core in here is currently based on.
