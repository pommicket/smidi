## sMIDI

A simple program for using MIDI controllers on POSIX systems. 

This is a replacement for programs like fluidsynth, because they're usually crazy and involve jack.
So here's an alternative that doesn't take over your system's audio!

This requires a SoundFont file, like the one which can be found here: https://member.keymusician.com/Member/FluidR3\_GM/index.html. This can be installed on Debian/Ubuntu with

```
apt install fluid-soundfont-gm
```
(as root)

Alternatively, you can just download and unzip it (move it to `/usr/share/sounds/sf2/FluidR3_GM.sf2`, and 
it will be used by default).

### Installing

```
make install
```
(as root)

To just compile it, without installing it, run:
```
make smidi_release
```
(note that just `make` on its own will compile a debug version)

### Usage

```
smidi [soundfont file]
```
