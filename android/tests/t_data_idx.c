// android/data_apk.c's index (plan item 301h: apk.py --data's assets/<name>.idx): sizes and paths read, a path with spaces kept, a last line without
// its newline; refused: a path with a ".." / "." / empty component, an absolute one, a backslash, a control character, no tab, a size that is not
// a number, more lines than room. Any component taken (-DDM_TEST_DATA_IDX_ANY: the "." / ".." / empty check left out): 2 fail.
#include "../data_apk.h"
#include <stdio.h>
#include <string.h>

static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static int parse(const char *t) { dm_data_ent e[8]; return dm_data_idx_parse(t, strlen(t), e, 8); }

int main(void) {
    dm_data_ent e[8]; const char *t = "70001\tData/Resources/unity default resources\n5\ta/x.bin\n4\ttop.txt";
    int k = dm_data_idx_parse(t, strlen(t), e, 8);
    CHECK(k == 3 && e[0].size == 70001 && !strcmp(e[0].path, "Data/Resources/unity default resources") && e[2].size == 4 && !strcmp(e[2].path, "top.txt"),
          "three files, a name with spaces, the last line without its newline (%d)", k);
    CHECK(parse("1\t../x\n") < 0 && parse("1\ta/../x\n") < 0, "a \"..\" component refused");
    CHECK(parse("1\t./x\n") < 0 && parse("1\ta//x\n") < 0 && parse("1\ta/\n") < 0, "a \".\" or empty component refused");
    CHECK(parse("1\t/etc/x\n") < 0, "an absolute path refused");
    CHECK(parse("1\ta\\\\x\n") < 0 && parse("1\ta\x01x\n") < 0, "a backslash or a control character refused");
    CHECK(parse("1 a\n") < 0 && parse("x1\ta\n") < 0 && parse("\ta\n") < 0, "no tab, a size that is not a number, no size: refused");
    { dm_data_ent one[1]; const char *two = "1\ta\n2\tb\n"; CHECK(dm_data_idx_parse(two, strlen(two), one, 1) < 0, "more lines than room: refused"); }
    CHECK(parse("") == 0 && parse("\n\n") == 0, "an empty index: no files");
    printf("t_data_idx: %d checks, %d failed\n", n, fail); return fail != 0;
}
