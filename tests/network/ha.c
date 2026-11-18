// SPDX-License-Identifier: MIT
#include "support.h"
#include "../../apps/ha/ha.c"
static void answer(const char *payload)
{
    app_on_data(REQUEST_SCREEN, (const uint8_t *)payload, (int32_t)strlen(payload), HOST_DATA_FIRST | HOST_DATA_LAST, 200);
}
int main(void)
{
    setting("every", "5 min");
    send_result = HOST_ERR_NO_SPACE;
    app_on_load();
    assert(!has_asked && last_status == HOST_FAILED_NO_MEMORY && redraws);
    send_result = HOST_OK; app_on_focus();
    assert(has_asked && sends == 2);
    answer("hero|21|sunny");
    int first_writes = writes;
    for (int poll = 0; poll < 288; poll++) answer("hero|21|sunny");
    assert(writes == first_writes);
    app_on_data(REQUEST_SCREEN, (const uint8_t *)"partial", 7, HOST_DATA_FIRST, 200);
    app_on_data(REQUEST_SCREEN, NULL, 0, HOST_DATA_FAILED, HOST_FAILED_NO_NETWORK);
    assert(!strcmp(screen, "hero|21|sunny"));
    app_on_data(REQUEST_SCREEN, (const uint8_t *)"orphan", 6, HOST_DATA_LAST, 200);
    assert(!strcmp(screen, "hero|21|sunny"));
    write_result = HOST_ERR_NO_SPACE; answer("hero|22|sunny"); assert(screen_dirty);
    write_result = HOST_OK; answer("hero|22|sunny"); assert(!screen_dirty);
    screen[0] = 0; app_on_load(); assert(!strcmp(screen, "hero|22|sunny"));
    send_result = HOST_ERR_ARGUMENT; ask();
    assert(last_status == HOST_FAILED_NOT_READY && !has_asked);
    puts("ha: refused enqueue, focus retry, 288 unchanged answers, interrupted stream and failed save retry passed");
}
