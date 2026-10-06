# Welcome to DualSOUP the spiritual successor to my abandoned 3DS emulator VitaminC

# Mission Statement
Improve preservation of the NDS/DSi/3DS library and their hardware via accurate emulation at ANY cost to usability and performance.

# Priorities
## Accuracy above all else
If the logic can be made 1 bit or one cycle more accurate I'm going to take the more accurate option.
I anticipate most sacrifices here will be due to lack of knowledge in the area.
## Maintainability
I need to be able to actually rewrite and fix things or else we're going to rapidly hit a brick wall.
## Performance
This is probably the lowest priority (for now) but it would be kinda sick if this emulator ran at full speed on high end cpus.
It currently runs at full speed *decently often* on my cpu (12700kf), but frequently suffers frametime spikes, or just straight up runs at 30fps in more intensive titles.

# Current Status
I would consider DualSOUP to be in an alpha state currently.
This means that things may be volatile and change dramatically with little notice.
Performance and accuracy may get significantly better or worse.
Configs and imports may be required to be manually rebuilt from scratch.

# Features
* Incomplete DS Lite emulation. (Very buggy and untested currently, and some heavily used ppu functionality hasn't even been implemented yet.)
* Support for flash, and eeprom sram DS cards.
* Partial support for infrared DS cards. (Only enough to make software that uses them boot, no actual emulation of ir comms or the internal chip is done currently.)

# Things I want to add in the future
* GBA emulation. (NDS style: ie. Reboots from DS mode, has all of the DS specific revisional quirks.)
* DSi emulation.
* 3DS emulation??? (Yes this means lle and cycle accurate. Yes I know that's a horrible idea. It'll be fun probably.)
* Basic LCD emulation. (Pixel grid, LCD persistence blurring, the odd colorspace used by some models.)
* Advanced LCD emulation. (Its possible to drive the LCD in an out-of-spec manner, which results in weird jank, seemingly varying with the LCD model.)
* Better GUI. (a lot of features are janky.)
* Better debugging features.
* The ability to actually save the game/firmware. (Kinda important.)

# Special Thanks
* melonDS team members: Largely responsible for me getting off my feet with regards to understanding how to do emudev and coding.
* The many folks who've worked on NDS/GBA homebrew and emudev: Could not have done any of this without the amount of research people before me had done on this/related consoles.
