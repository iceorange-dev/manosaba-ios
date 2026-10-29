// libc passthrough table: C library functions consistent with AAPCS64 and with no struct layout dependency, connected directly to Apple libc. Goes into the swhle section.
// L: called direct, on the guest's stack (memory, string and math functions: hot and shallow; host/tests/t_hstack measures each one's stack use);
// LS: on the thread's host stack like every other host function (number parsing and time: locale and time zone code, a time zone load is deep).
#include "dm.h"
#include "table.h"
#include <math.h>
#include <ctype.h>
#include <time.h>
typedef struct { const char *name; void *fn; int direct; } hle_ent;   // = hle.h
#define L(n) DM_TABLE(swhle) static const hle_ent hle_lib_##n = { #n, (void *)n, 1 };
#define LS(n) DM_TABLE(swhle) static const hle_ent hle_lib_##n = { #n, (void *)n, 0 };
L(memcpy) L(memset) L(memmove) L(memcmp) L(memchr) L(strlen) L(strnlen) L(strcmp) L(strncmp)
L(strcpy) L(strncpy) L(strcat) L(strncat) L(strchr) L(strrchr) L(strstr) L(strspn) L(strcspn)
L(strpbrk) L(strcasecmp) L(strncasecmp) LS(atoi) LS(atof) LS(strtol) LS(strtoul) LS(strtoll) LS(strtoull)
LS(strtod) LS(strtof) L(sqrtf) L(cbrtf) L(expf) L(acosf) L(cosf) L(sinf) L(atan2f) L(fmodf) L(cos)
L(sin) L(powf) L(atanf) L(tanf) L(asinf) L(log10) L(pow) L(logf) L(ldexpf) L(log10f) L(log)
L(ldexp) L(frexp) L(atan2) L(modff) L(exp2f) L(exp2) L(log2f) L(log2) L(lroundf)
LS(mktime) LS(timegm) LS(gmtime_r) LS(localtime_r) LS(strftime)
