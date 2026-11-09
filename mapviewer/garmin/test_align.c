#include "align.h"
#include <assert.h>

int main(void)
{
    const unsigned char bytes[] = {0xff, 0x12, 0x34, 0x56, 0x80};
    assert(get_u8(bytes + 1) == 0x12);
    assert(get_u16(bytes + 1) == 0x3412);
    assert(get_u24(bytes + 1) == 0x563412);
    assert(get_u32(bytes + 1) == 0x80563412U);
    assert(get_u24(bytes + 2) == 0x805634);
    return 0;
}
