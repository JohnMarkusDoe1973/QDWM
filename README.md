# QDWM

Quick n Dirty Window Manager.

QDWM is a small personal X11 window manager written in C. It is not polished.
It manages windows, tiles them, draws a bar, handles a few keybinds, and stays out of the way.
***This software NEEDS a config.h file. I attatch a sample config.h later on.***

## Features

- X11 window management
- Tiling and floating modes
- 5 workspaces
- Scratchpad terminal
- Simple status bar
- Volume display
- mpv song display
- Keyboard-driven workflow
- Around 655 lines of C
- Around 3 MB RSS *(on my machine)*

## Warning

This is personal software. It is built for my setup and may break yours.
Run it in Xephyr before using it as your actual window manager.

## Dependencies

- Xlib
- a C compiler
- `pactl` for volume status
- `mpv` and `socat` for media display/control
- `dmenu` for launching music

## Build

```sh
make clean
make
(sudo) make install
```

## Run

exec qdwm in .xinitrc

## Config.h example

***This software NEEDS a config.h file. Below I attatch a sample config.h:***

```c
#define maxwindows 32
static const char *termcmd   = "st";
static const char *launchcmd = "dmenu_run";
static const char *scratchpadcommand = "st -c scratchpad -e nano /tmp/scratchpad.txt";
static const int gap = 10;
static const unsigned int gridsize = 2;
static const unsigned int modkey = Mod1Mask;
static const unsigned int move_button   = 1;
static const unsigned int resize_button = 3;
static const unsigned int border_width = 2;
static const unsigned long border_focus  = 0xe4bfd7;
static const unsigned long border_normal = 0x8b8391;
static const unsigned long barbg = 0x38323f;
static const unsigned long barfg = 0xf7eef8;
static const int bar_height = 20;
```

## Please don't use it if you value your sanity.
