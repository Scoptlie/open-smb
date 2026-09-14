
# OpenSMB

OpenSMB is an open-source re-implementation of Super Mario Bros., written in C.

This project is a work-in-progress. The current version features a machine translation of the game's code based on doppelganger's [disassembly of the NTSC ROM](https://gist.github.com/1wErt3r/4048722).

The goal for this project is to replace all machine-generated code with equivalent human-readable code.

## Building

### Linux

Have these dependencies installed:
- Clang >= 20
- SDL3 >= 3.4.2
- OpenGL 4.6
- OpenAL 1.1

Then run `make` in the project directory. This will generate the executable `smb`

### Windows

Windows is not currently supported, but it will be in the future!

## How it works

All of the game's code runs in pure native C. As of now, it is still very close to the disassembly.

It is supported by a C runtime that includes global variables for 6502 registers, RAM, and a full copy of the original ROM's PRG section (see [source/compat.c]()). The original PRG section is only present for read-only data, no code from it is executed.

The C runtime also emulates the NES audio, video, and controller input components (see [source/env.c]()).

## Translator

The translator that generated the original C code is in [this repository]().

## Acknowledgements

Thank you to doppelganger for their original [Super Mario Bros. disassembly](https://gist.github.com/1wErt3r/4048722).

Thank you to Andy McFadden for converting doppelganger's disassembly into a [SourceGen project](https://6502disassembly.com/nes-smb/), which was very helpful as it allowed me to regenerate the assembly code in a format that better worked for my translator.

## To-do

- The immediate next step is to develop a way of testing the correctness of any re-implemented code. This will likely be done by replaying inputs and verifying video and audio output using checksums, although this will require a comprehensive TAS that covers all of the game's code.
