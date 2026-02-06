# ansipixels-c

A C library for rendering fast Terminal User Interfaces (TUIs)
using ANSI codes. Inspired by the Go library
https://pkg.go.dev/fortio.org/terminal/ansipixels

You can download demo binaries and *.h and libansipixels.a from [releases/] or build from source.

See [fps.c](fps.c) and [ansipixels.c](ansipixels.c) for example, or, for instance with prog.c being:
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
gcc -I. -Wall -Wextra prog.c  -L. -lansipixels
./a.out
Terminal in raw mode - 101 x 35
```

<hr/>

(C) 2026 Laurent Demailly <ldemailly at gmail> and contributors.

No warranty implied or expressly granted. Licensed under Apache 2.0 (see [LICENSE](LICENSE)).
