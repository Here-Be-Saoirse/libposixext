# libposixext
Posixly questionable implementations of helpful functions (mostly used in the FractalKit Solaris 10 upgrade kit)
## What the hell is this?
This is libposixext, a library containing a few helpful (at the moment, memory and time-related) functions to patch
over Solaris 10's less than complete libc. This was hacked together in a few hours and we make absolutely zero
promises as to the posixness, usefulness, readability or lickability of this code. If your machine detonates because
you included this library, then that might say some questionable things about your machine.
## How to build?
A Makefile is provided:
```
make
```
Or manually:
```
cc -fPIC -shared -o libposixext.so posixext.c
```
You can override the compiler and flags:
```
make CC=gcc CFLAGS="-O2 -Wall"
```
## Install?
```
make install
```
Installs `libposixext.so` to `/usr/local/lib` and `posixext.h` to `/usr/local/include` by default.
Override with `PREFIX=/your/path make install`.
## Usage?
Install libposixext.so into your favourite library directory, and posixext.h into where your includes go.
Do a `#include <posixext.h>` and add `-lposixext` to your ldflags.
## Forks and whatever?
Go ahead. It's CDDL for a reason
