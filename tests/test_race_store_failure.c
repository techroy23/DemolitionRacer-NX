#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "draft_signatures.h"

static jmp_buf stop_game;
static unsigned calls, after_store, second_ot;
static sint32 store_result;
static uint32 page, enabled = 1, mask_count;
static uint32 rectangle[2], store_buffer[128], ot;
void game_800B3810(void);
void *psx_addr(uint32, size_t);
void *psx_addr(uint32 address, size_t bytes)
{
    switch (address) {
        case 0x80065128u: return &page;
        case 0x800C768Cu: return &enabled;
        case 0x800C7450u: return &mask_count;
        case 0x8008BC48u: return rectangle;
        case 0x800C73FCu: return &ot;
        default:
            assert(address >= 0x800C7488u && address - 0x800C7488u + bytes <= sizeof(store_buffer));
            assert(bytes == 512 || after_store);
            return (unsigned char *)store_buffer + (address - 0x800C7488u);
    }
}
sint32 StoreImage(PSX_RECT *rect, uint32 *pixels)
{
    (void)rect;
    assert(pixels == store_buffer);
    ++calls;
    if (!store_result) memset(pixels, 255, sizeof(store_buffer));
    return store_result;
}
uint32 draft_agent3_missing_8003D9AC(uint32 mode) { assert(!mode); return ++after_store; }
void DrawOTag(uint32 *tag) { assert(tag == &ot); ++second_ot; }
_Noreturn void switch_abort_game(const char *reason)
{
    assert(strstr(reason, "StoreImage"));
    longjmp(stop_game, 1);
}
int main(void)
{
    store_result = -1;
    mask_count = 123;
    if (!setjmp(stop_game)) {
        game_800B3810();
        assert(!"must stop before using failed StoreImage");
    }
    assert(calls == 1 && !after_store && !second_ot && mask_count == 123);
    store_result = 0;
    game_800B3810();
    assert(calls == 2 && after_store == 1 && second_ot == 1 && mask_count == 256);
    puts("Actual race StoreImage consumer: failure unwinds before read/second OT; success unchanged");
}
