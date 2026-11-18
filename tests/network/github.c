// SPDX-License-Identifier: MIT
#include "support.h"
#include "../../apps/github/github.c"
int main(void)
{
    setting("login", "octocat"); setting("token", "test-token-not-a-secret");
    refresh();
    assert(sends == 1 && strstr(request_body, "contributionLevel date"));
    assert(!strstr(request_body, "contributionCount"));
    assert(strstr(request_body, "totalContributions") && strstr(request_body, "octocat"));

    /* Resuming an entered scene does not request data; an update must. */
    session_value = 1;
    session_length = sizeof session_value;
    state = (state_t){ .version = VERSION, .status = HOST_FAILED_NO_ANSWER };
    app_on_focus();
    assert(sends == 1);
    app_on_updated();
    assert(sends == 2 && !state.has_data && state.status == HOST_FAILED_NO_ANSWER);
    app_on_focus();
    assert(sends == 2);

    /* The request keeps the last complete calendar visible until an answer. */
    state = (state_t){ .version = VERSION, .has_data = 1, .status = 200,
        .login_hash = login_hash("octocat"),
        .calendar = { .first_day = 20000, .count = 366, .total = 42 } };
    state_t before = state;
    int before_writes = writes;
    app_on_updated();
    assert(sends == 3 && memcmp(&state, &before, sizeof state) == 0);
    assert(writes == before_writes);
    puts("github: compact query, immediate update refresh, entered-session resume and cached calendar preservation");
}
