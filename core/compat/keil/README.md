# Optional C251 compatibility inputs

These files accompany MX251. Applications choose them explicitly; none is linked automatically.

* `omf251.py` reads C251/A251 objects and libraries and emits the tested relocatable subset as SDAS251 assembly. Run `python omf251.py input.obj --inventory report.json --assembly imported.asm`, then assemble with `sdas251 -plosgff imported.rel imported.asm`. Each input segment has its own area; the link driver must place those areas. Public symbols use `__omf_` plus the ASCII name in hexadecimal, with no automatic C ABI conversion. It currently supports relocatable CODE, DATA, EDATA and XDATA, near/far pointer data, external calls, and local relative branches. This is a standalone object conversion stage; Build-CMake does not yet import vendor libraries automatically. See [USB OMF research](../../../docs/USB-OMF-P083.md) for the remaining full-library and ABI work.

* `uart1.c` and `uart1.h` supply `putchar` and `_getkey` using the standard UART1 SBUF/RI/TI registers. The application configures baud rate and initially sets TI. Output inserts CR before LF and implements XON/XOFF flow control. The provider uses polling; do not combine it with an interrupt handler that consumes the same flags.
* `vector_aliases.py:translate` converts a file containing numeric `CSEG AT ...H` / `LJMP ...H` vector redirects into naked MX251 C interrupt entries. A wrapper executes EJMP to the destination vector in bank FF and retains the hardware interrupt frame. Return value `None` means the file needs the normal assembler dialect converter. The extra jump is included in firmware size.

MX251's function collection follows references to function symbols from calls, immediate addresses, constant/data tables and archive members. This also retains targets used through ordinary C function pointers. Code entered through a fixed numeric address supplied outside those references must be listed in `FunctionGcKeep`.

The general compiler changes supporting CODE tentative definitions, brace-elided aggregates, EDATA automatic variables and binary32 standard math names are in the compiler/header sources. They do not depend on these compatibility files or on an SDK project name.
