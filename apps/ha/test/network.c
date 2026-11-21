// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../ha.c"
#include "standin.h"

/* Every kv_set the application tried, refused or not. */
static int writes(void) { return standin()->store_writes + standin()->refused_writes; }

/* A whole answer in one piece. */
static void answer(const char *payload)
{
    standin_data(REQUEST_SCREEN, -1, 200, payload, (int32_t)strlen(payload), HOST_DATA_CHUNK_MAX, STANDIN_WHOLE, 0);
}
int main(void)
{
    standin_reset();
    standin_handlers.on_data = app_on_data;
    standin_set("every", "5 min");
    standin()->refuse_requests = HOST_ERR_NO_SPACE;
    app_on_load();
    assert(!has_asked && last_status == HOST_FAILED_NO_MEMORY && standin()->redraws);
    standin()->refuse_requests = HOST_OK; app_on_focus();
    assert(has_asked && standin()->sends == 2);
    answer("hero|21|sunny");
    int first_writes = writes();
    for (int poll = 0; poll < 288; poll++) answer("hero|21|sunny");
    assert(writes() == first_writes);
    /* The first piece, and then the request fails. */
    standin_data(REQUEST_SCREEN, -1, 200, "partial", 7, 7, STANDIN_FAILED, HOST_FAILED_NO_NETWORK);
    assert(!strcmp(screen, "hero|21|sunny"));
    /* A last piece of an answer that never began: the host cuts none like it, so it is handed over as it is. */
    app_on_data(REQUEST_SCREEN, (const uint8_t *)"orphan", 6, HOST_DATA_LAST, 200);
    assert(!strcmp(screen, "hero|21|sunny"));
    standin()->refuse_writes = HOST_ERR_NO_SPACE; answer("hero|22|sunny"); assert(screen_dirty);
    standin()->refuse_writes = HOST_OK; answer("hero|22|sunny"); assert(!screen_dirty);
    screen[0] = 0; app_on_load(); assert(!strcmp(screen, "hero|22|sunny"));
    standin()->refuse_requests = HOST_ERR_ARGUMENT; ask();
    assert(last_status == HOST_FAILED_NOT_READY && !has_asked);
    puts("ha: refused enqueue, focus retry, 288 unchanged answers, interrupted stream and failed save retry passed");
}
