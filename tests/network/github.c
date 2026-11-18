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
    puts("github: query preserves total, dates and levels without unused daily counts");
}
