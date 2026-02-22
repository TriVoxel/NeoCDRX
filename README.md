# NeoCD RX
[_**Check out the original project by niuus!**_](https://github.com/niuus/NeoCDRX/)

**_NeoCD RX_** is a _Neo Geo CD / Neo Geo CDZ_ emulator for the _GameCube_, _Wii_, and _Wii U_'s
Virtual Wii. It owes its existence to various other emulators:
**_NEO-CD REDUX_** (Softdev), **_NeoGeo CD Redux_** (Infact), **_NeoCD-Wii_** (Wiimpathy
/ Jacobeian), **_NeoCD Redux Unofficial_** (megalomaniac). **_NEO-CD REDUX_** was itself
based on **_NeoCD/SDL_** 0.3.1 (Foster) and **_NeoGeo CDZ_** (NJ) emulator,
which are source ports of the original **_NeoCD_** emulator (Fabrice Martinez).

**_NeoCD RX_** is a "homebrew application" which means you will need a way to run unsigned code on your Nintendo Wii. The best website for getting started with Wii homebrew is WiiBrew (www.wiibrew.org).

Based/forked from:
https://github.com/emukidid/neogeo-cd-redux

(Under GPL License)


## FEATURES

* Z80 emulator core 3.6
	* M68000 emulator core 3.3
	* Wii Remote, Wii Remote Plus, Wii Remote+Nunchuk, and GameCube controller support
	* SD/SDHC, USB, WKF (GameCube), IDE-EXI V1, DVD support
	* UStealth USB devices support
	* Region select for uncut content and extra languages (USA / Europe / Japan)
	* Neo Geo CD Virtual Memory Card (8KiB battery-backed SRAM chip) support. Save directly to SD/USB or to your physical GameCube card for max nostalgia!
	* Sound FX / Music 3-Band equalizer
	* Super fast loading times. Original console weakness is no more!
	* Extensive options to suit every play style
* Available in various skins/colors
* Open Source!


## RECENT CHANGELOG

[1.1.00 - Feb 22, 2026]
* Improved hardware support
	* Added support for GCLoader & CubeODE
	* Added support for exFAT SD cards
	* Faster loading times thanks to libogc2 & libdvm upgrade
* Improved settings
	* Added settings save/load
	* Added filtering options
		* Re-written GX pipeline gives you pixel perfect integer scaling.
		* You can use nearest neighbor filtering for ultra-sharp, perfectly-crisp graphics, or bilinear to smooth out the jaggies!
	* Added option to skip Neo Geo BIOS
	* Added "Crop Overscan" option to hide unintended graphical glitches
	* You can now set a preferred controller button combo or button to bring up the main menu
	* You can now set a default load device so you can jump into games even faster!
	* You can nowforce progressive scan or interlacing!
* Upgraded user interface
	* Unusable buttons are now hidden
	* Improved file browser
	* Selected button stands out better (Pretty rainbowssssss!!!!!!)
* Changed button mappings to make them more intuitive
* Bug fixes
	* Fixed bug where a power cycle was required to re-launch NeoCDRX after closing it.
	* Fixed a bug in the file browser where a page could be empty if you scrolled it weirdly
	* Other minor bug fixes
* The entire experience has been overhauled!
* Huge thank you to niuus for maintaining the original. I couldn't have done it without them, or all the other amazing and talent people who built the foundation of this software. Check out the in-app credits menu!

[older update history in the **NeoCDRX_manual.pdf**]
https://github.com/niuus/NeoCDRX/blob/main/NeoCDRX_manual.pdf


## INSTALLATION AND USE

An SD card is highly recommended. To use NeoCD-RX on the GameCube, you will need to extract the .zip file to the root of your SD card. You can rename `neocd-rx.dol` to whatever you want, but make sure you don't rename the folders.

Finally, you need to obtain a proper dump of the _Neo Geo CD/CDZ_ console BIOS.
Copy the file inside the "**_\NeoCDRX\bios_**" directory and name it "**_NeoCD.bin_**".
The emulator only works with the following:

```
Neo Geo CDZ BIOS (NeoCD.bin)
Size: 524.288 bytes
CRC32: DF9DE490
MD5: F39572AF7584CB5B3F70AE8CC848ABA2
SHA-1: 7BB26D1E5D1E930515219CB18BCDE5B7B23E2EDA
```
```
Neo Geo CDZ BIOS (NeoCD.bin)
Size: 524.288 bytes
CRC32: 33697892
MD5: 11526D58D4C524DAEF7D5D677DC6B004
SHA-1: B0F1C4FA8D4492A04431805F6537138B842B549F
```

Once you are done, you can proceed to run the emulator. Additionally, you can
install the NeoCD-RX Forwarder Channel in your _Wii_ or _vWii_ System Menu, or the
special NeoCD-RX Channel for Wii U, which reads the configuration and necessary
files from your device "**_\NeoCDRX_**" folder, be it SD or USB.


## CONFIGURATION

To configure NeoCD-RX, press 'A' on the "Settings" box. This will bring up a
screen where you can configure the following:
- Region
	- Will allow you to change the emulated console region, to access other languages and in some cases, change or uncensor game content (fatalities, blood, difficulty, lives, title screens, etc.). Reload the game (not reset) for the setting to take effect.
- Save Device
	- Offers two options, use "SD/USB" to save the SRAM memory (sort of a virtual memory card implemented inside the real Neo Geo CD console) directly to the media drive, or use "MEM Card" to save to a physical GameCube Memory Card, as you would on a real Neo Geo AES, to take your progress to another console, or just for the nostalgia factor.
- Menu Toggle
	- Allows you to customize the gamepad buttons which will pause emulation and bring you back to the main menu
- Skip BIOS
	- Skips the Neo Geo BIOS animation. Saves a lot of time loading into games, but you miss out on the nostalgia. );
- FX / Music Equalizer
	- Allows you to raise the volume on sound FX or MP3
tracks, or raise the gain in Low / Mid / High frequencies to your liking.
- Graphics Settings
	- Crop overscan
		- Shows or hides the left and right edges of the screen which were normally hidden by old CRTs and aren't meant to be seen. Disabling this will reveal graphical artifacts present in the original games.
	- Filter Mode
		- Lets you choose between "Bilinear" sampling and "Nearest" (nearest neighbor) sampling. Bilinear smooths things out to give them a more natural and authentic look. Nearest neighbor gives you sharp, clean, pixel perfect graphics in the highest definition.
	- Video Mode
		- Can either automatically detect the best video mode (recommended), or lets you override it to force progressive or interlaced video.


## PREPARING THE GAMES FOR USE WITH THE EMULATOR

_Note: This is planned to change in the near future. I am planning some big changes to streamline this process, but will offer backwards compatibility to users who go through the trouble of setting it up this way._

For every game disc, you need to create a subdirectory inside the included "**_\NeoCDRX\games_**" named whatever you like, and copy all the game data files there. Inside this folder, create another subdirectory called "**_mp3_**", where you have to copy your music tracks. **IMPORTANT**: even if you won't use the music, the folder is needed.

The music tracks need to be encoded from the original CD's Red Book standard 44.1 kHz WAV, to MP3 format (128kbps minimum, or better), named exactly "**_TrackXX.mp3_**" where XX is a number that always starts at 02, as the data track is always 01. Free CD audio ripping software is readily available.

Examples and pictures are inside the **NeoCDRX_manual.pdf**
https://github.com/niuus/NeoCDRX/blob/main/NeoCDRX_manual.pdf

After this, you are more than ready to start playing. Each game folder you make will be treated by the emulator as a full CD.


## SUPPORTED CONTROLLERS

NeoCD RX currently supports the following:

```
	• GameCube controller

	• Wii Remote (horizontal)

	• Wii Remote Plus (or Wii MotionPlus adapter)

	• Wii Remote+Nunchuk
```

## DEFAULT MAPPINGS

### GameCube Controller
			Neo Geo A = A
			Neo Geo B = B
			Neo Geo C = X
			Neo Geo D = Y
			Neo Geo Select = Z
			Neo Geo Start = START
			Neo Geo directions = D-pad or Analog Stick
### Wii Remote (horizontal)
			Neo Geo A = 1
			Neo Geo B = 2
			Neo Geo C = B
			Neo Geo D = A
			Neo Geo Select = MINUS (-)
			Neo Geo Start = PLUS (+)
			Neo Geo directions = Dpad (horizontal)
### Wii Remote+ Nunchuk
			Neo Geo A = A
			Neo Geo B = B
			Neo Geo C = PLUS (+)
			Neo Geo D = 1
			Neo Geo Select = MINUS (-)
			Neo Geo Start = PLUS (+)
			Neo Geo directions = Analog Stick


## EMULATOR MAPPINGS

_Force saving to Virtual Memory Card (while in-game, for the games that
support it)_
```
"R" button (GameCube controller)
"PLUS (+)" and "MINUS (-)" buttons together (Wii Remote / Wii Remote+Nunchuk)
```
_Navigation_
```
D-pad or Left Analog Stick (GameCube controller)
D-pad (Horizontal Wii Remote)
D-pad or Nunchuk Analog Stick (Wii Remote+Nunchuk)
```
_Enter directory or Menu option / Change setting_
```
"A" button (GameCube controller)
Button "2" (Wii Remote / Wii Remote+Nunchuk)
```
_Go back from any Menu_
```
"B" button (GameCube controller)
Button "1" (Wii Remote / Wii Remote+Nunchuk)
```
_Go back from Game List_
```
"Z" button (GameCube controller)
Button "HOME" (Wii Remote / Wii Remote+Nunchuk)
```
_Navigate one page backwards on the Game List (when you have more than 8 titles)_
```
"L" button or d-pad left (GameCube controller)
"MINUS (-)" button (Wii Remote / Wii Remote+Nunchuk)
```
_Navigate one page forward on the Game List (when you have more than 8 titles)_
```
"R" button or d-pad right (GameCube controller)
"PLUS (+)" button (Wii Remote / Wii Remote+Nunchuk)
```
_Mount and run a valid game directory_
```
"A" button (GameCube controller)
Button "2" (Wii Remote / Wii Remote+Nunchuk)
```
_Failsafe video mode (Force Menu to 480i with Component / Digital cable)_
```
Hold "L" button right before the emulator is loading to activate
```


## CREDITS & THANKS
```
• NeoCD-Wii (Wiimpathy / Jacobeian)
• NeoCD Redux Unofficial (megalomaniac)
• NeoGeo CD Redux (Infact)
• NEO-CD REDUX (softdev)
• NeoCD/SDL 0.3.1 (Foster)
• NeoGeo CDZ (NJ)
• NeoCD 0.8 (Fabrice Martinez)
• [M68000 C Core](https://github.com/kstenerud/Musashi) (Karl Stenerud)
• [MAME Z80 C Core](https://github.com/mamedev/mame/tree/master/src/devices/cpu/z80) (Juergen Buchmueller)
• Sound Core (MAMEDev.org)
• The EQ Cookbook (Neil C / Etanza Systems)
• The EQ Cookbook (float only version code - Shagkur)
• WKF & IDE-EXI V1 (code from [Swiss GC](https://github.com/emukidid/swiss-gc) - emu_kidid)
• libMAD (Underbit Technologies)
• libZ (zlib.org)
• TehSkeen forum (2006-2009)
• NeoCDRX emu bg - Style 1 (catar1n0)
• NeoCDRX menu design (NiuuS)
• NeoCDRX v1.1.0 revamp (TriVoxel)
```


## RELEVANT LINKS

* Newest/Latest NeoCDRX release at:
https://github.com/trivoxel/NeoCDRX/releases
