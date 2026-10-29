// Tables of host entries the HLE macros fill (hle.h HLE / HLE_FORCE, hle_table.c), read back whole (hle.c resolve_import, loader.c force_init):
// a Mach-O section __DATA,<table> on Apple; an ELF section <table> elsewhere, whose bounds the linker gives as __start_<table> / __stop_<table>
// (plan item 301c)
#pragma once
#ifdef __APPLE__
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#define DM_TABLE(t) __attribute__((used, section("__DATA," #t)))
#define DM_TABLE_GET(t, n) ((const void *)getsectiondata(&_mh_execute_header, "__DATA", #t, (n)))
#else
#define DM_TABLE(t) __attribute__((used, retain, section(#t)))
#define DM_TABLE_GET(t, n) ({ extern const char __start_##t[] __attribute__((weak)), __stop_##t[] __attribute__((weak)); \
                              *(n) = (unsigned long)(__stop_##t - __start_##t); (const void *)__start_##t; })
#endif
