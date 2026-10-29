// The host's assembly (entry.S, hstack.S, gfx/nvn's nvn_entry.S and nvn_thunks.S) as Mach-O on Apple and as ELF elsewhere (plan item 301c): C(n) a C
// symbol (Mach-O prefixes an underscore), PAGE(n) / PAGEOFF(n) the adrp / add halves of its address, L(n) a local label, CSTRING the section of
// C strings, MACH_TICKS the function giving mach_absolute_time's ticks (on Android a function of plat_linux.c: the shim's is inline), and SLOTBASE r
// the thread slot block's base (slots.h): Darwin's TSD at tpidrro_el0, Android's block at bionic's TLS_SLOT_APP (tpidr_el0 + 16).
#ifdef __APPLE__
#define C(n) _##n
#define PAGE(n) _##n@PAGE
#define PAGEOFF(n) _##n@PAGEOFF
#define L(n) L##n
#define CSTRING .cstring
#define MACH_TICKS _mach_absolute_time
.macro SLOTBASE r
    mrs \r, tpidrro_el0
.endm
#else
#define C(n) n
#define PAGE(n) n
#define PAGEOFF(n) :lo12:n
#define L(n) .L##n
#define CSTRING .section .rodata.str1.1, "aMS", %progbits, 1
#define MACH_TICKS dm_mach_ticks
.macro SLOTBASE r
    mrs \r, tpidr_el0
    ldr \r, [\r, #16]
.endm
    .section .note.GNU-stack, "", %progbits   // no executable stack
#endif
