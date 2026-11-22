# Welcome to DualSOUP
**WIP Cycle Accurate NDS Emulator**

# Mission Statement
Improve preservation of the NDS/DSi/3DS library and their hardware via accurate emulation at ANY cost to usability and performance.

# Priorities
## Accuracy above all else
If the logic can be made 1 bit or one cycle more accurate I'm going to take the more accurate option.

I anticipate most sacrifices here will be due to lack of knowledge in the area.

## Maintainability
I need to be able to actually rewrite and fix things or else this project is going to rapidly hit a brick wall.

## Performance
This is probably the lowest priority (for now) but it would be kinda sick if this emulator ran at full speed on high end cpus.

~~It currently runs at full speed *decently often* on my cpu (12700kf), but frequently suffers frametime spikes, or just straight up runs at 30fps in more intensive titles.~~

It currently runs extremely poorly (15 ~ 30 fps most of the time) in NDS mode, but should run at or close to full speed with a decently high end cpu in GBA mode.

I think this can be improved greatly, but its a low priority for now.

# Current Status
I would consider DualSOUP to be in an alpha state currently.

This means that things are volatile and may change dramatically with no notice.

Performance and accuracy may get significantly better or worse.

Configs and imports may be required to be manually rebuilt from scratch.

# Features
* Incomplete DS Lite emulation. (Very buggy and untested currently, and some important graphics functionality has yet to be implemented.)
* Near complete GBA mode support. (Largely untested, but should run most software you throw at it. Requires booting via the NDS firmware.)
* Support for flash, and eeprom/fram DS Cards.
* Partial support for infrared DS Cards. (Only enough to make software that uses them run, no actual emulation of ir comms or the internal chip is done currently.)
* Support for eeprom, sram/fram and flash GBA Paks.

# Things I want to add in the future
* DSi emulation.
* 3DS emulation??? (Yes this means lle and cycle accurate. Yes I know that's a horrible idea. It'll be fun probably.)
* Basic LCD emulation. (Pixel grid, LCD persistence blurring, the odd colorspace used by some models.)
* Advanced LCD emulation. (Its possible to drive the LCD in an out-of-spec manner, which results in weird jank, seemingly varying with the LCD model.)
* Better GUI. (a lot of features are janky.)
* Better debugging features.

# Special Thanks
* melonDS team members: Largely responsible for me getting off my feet with regards to understanding how to do emudev and coding.
* The many folks who've worked on NDS/GBA homebrew and emudev: Could not have done any of this without the amount of research people before me had done on this/related consoles.

# Build Instructions
## Linux:
1. Install reqs: clang, clang++, SDL3, make
3. type make in the directory with the makefile
4. ????
5. Profit
## MacOS:
1. Please see Linux steps
2. Pray
3. Idk i dont use MacOS i just know it works
## Windows:
1. use msys2 or some other unix compatible terminal
2. Please see MacOS steps 1 and 2
3. Pray++
4. It doesn't statically link anything so you will probably need to run it from the terminal you built it in. im sorry, i dont know how to fix that.
