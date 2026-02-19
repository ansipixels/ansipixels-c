[![CI Checks](https://github.com/ansipixels/ansipixels-c/actions/workflows/ci.yml/badge.svg)](https://github.com/ansipixels/ansipixels-c/actions/workflows/ci.yml)
[![GitHub Release](https://img.shields.io/github/release/ansipixels/ansipixels-c.svg?style=flat)](https://github.com/ansipixels/ansipixels-c/releases/)

# ansipixels-c

A C library for rendering fast Terminal User Interfaces (TUIs)
using ANSI codes. Inspired by the Go library
https://pkg.go.dev/fortio.org/terminal/ansipixels

Currently Unix / POSIX only (with high resolution timer optimization for macOS (darwin)).

You can download demo binaries and *.h and libansipixels.a from [releases](releases/) or build from source.

See [demos](demos/): [fps.c](demos/fps.c) and [main.c](demos/main.c) for example, or, for instance with prog.c being:
```c
#include "ansipixels.h"

int main(void) {
     // sets terminal to raw and will install atexit to restore terminal
    ap_t ap = ap_open();
    if (ap == NULL) {
        return 1; // error already logged
    }
    dprintf(STDOUT_FILENO, "Terminal in raw mode - %d x %d\n", ap->w, ap->h);
    return 0;
}
```
and
```sh
gcc -I./include -Wall -Wextra prog.c  -L. -lansipixels
./a.out
Terminal in raw mode - 101 x 35
```


See [record](demos/record.c) for a interesting demo of interception of TUI and recording of stats. For instance:
```sh
make clean record DEBUG=0 SAN=
./record --hud -- go run fortio.org/terminal/fps@latest -fire -truecolor
```
<hr/>

(C) 2026 Laurent Demailly <ldemailly at gmail> and contributors.

No warranty implied or expressly granted. Licensed under Apache 2.0 (see [LICENSE](LICENSE)).
