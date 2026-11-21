// SPDX-License-Identifier: MIT
/*
 * The application's own handlers over a store charged as Perinkpheral OS charges
 * it: which row an answer updates, the order, the record at the longest
 * list, what is written and when, a changed list, scrolling.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../status.c"
#include "standin.h"

enum { NOW = 1794955920 };
static int answers_writes, checked_writes, other_writes, seen_writes;
static int64_t clock_s = NOW;

/* The host counts its writes together. Which key each was is told by what changed:
 * a write that changed neither record, or was of another key, is counted apart. */
typedef struct { uint8_t bytes[HOST_KV_VALUE_MAX]; int32_t length; } kept_t;
static kept_t kept_answers = { .length = -1 }, kept_checked = { .length = -1 }; /* -1: not stored */

static int stored_length(const char *key)
{
    int32_t length = 0;
    return standin_value(key, &length) != NULL ? length : -1;
}
static int changed(const char *key, kept_t *kept)
{
    int32_t length = 0;
    const uint8_t *bytes = standin_value(key, &length);
    if (bytes == NULL) { length = -1; }
    if (length == kept->length && (length <= 0 || !memcmp(bytes, kept->bytes, (size_t)length))) return 0;
    kept->length = length;
    if (length > 0) memcpy(kept->bytes, bytes, (size_t)length);
    return 1;
}
static void tally(void)
{
    int written = standin()->store_writes - seen_writes;
    int answers = changed("answers", &kept_answers), checked = changed("checked", &kept_checked);
    seen_writes = standin()->store_writes;
    answers_writes += answers;
    checked_writes += checked;
    other_writes += written - answers - checked;
    /* No key or value over the host's limits, and the store never over. */
    assert(standin()->refused_writes == 0);
}
static int redraws(void) { return standin()->redraws; }
/* What the page stores: a setting, an empty one too. */
static void user_sets(const char *key, const char *value) { standin_set(key, value); }
static void settings(void) { app_on_settings(); tally(); }

/* An answer as the SDK's picker reads it; a value that is NULL is not in it. */
static void answer(int32_t item, int32_t http_status, const char *name, const char *indicator)
{
    char body[256];
    int32_t length = snprintf(body, sizeof body, "{\"page\":{%s%s%s},\"status\":{%s%s%s}}",
                              name != NULL ? "\"name\":\"" : "", name != NULL ? name : "", name != NULL ? "\"" : "",
                              indicator != NULL ? "\"indicator\":\"" : "", indicator != NULL ? indicator : "",
                              indicator != NULL ? "\"" : "");
    standin_clock(clock_s);
    standin_picked(0, item, http_status, http_status >= 0 ? body : NULL, http_status >= 0 ? length : 0);
    tally();
}
/* The instance is unloaded and loaded again: nothing but the store is left. */
static void reload(void)
{
    memset(services, 0xA5, sizeof services);
    memset(record, 0xA5, sizeof record);
    count = -1; checked_at = 0; list_hash = 0; first_row = 0;
    standin_clock(clock_s);
    app_on_load();
    tally();
}
static void round_of(int32_t services_count, const char *indicator)
{
    char name[24]; /* "Page " and any int32_t */
    for (int32_t item = 0; item < services_count; item++) {
        snprintf(name, sizeof name, "Page %d", item);
        answer(item, 200, name, indicator);
    }
}

int main(void)
{
    standin_reset();
    standin_handlers.on_picked = app_on_picked;
    standin()->is_session_writable = true; /* app_on_blur() is called as for the instance on the screen */
    /* A list of four; an answer is its item's row and nobody else's. */
    user_sets("services", "www.githubstatus.com\nwww.cloudflarestatus.com\ndiscordstatus.com\nstatus.openai.com");
    reload();
    assert(count == 4 && !strcmp(listed + item_at[2], "discordstatus.com"));
    answer(2, 200, "Discord", "major");
    assert(!strcmp(services[2].name, "Discord") && services[2].indicator == 2 && services[2].updated_at == NOW);
    assert(!(services[0].flags | services[1].flags | services[3].flags) && answers_writes == 1 && checked_writes == 0);
    answer(0, 200, "GitHub", "none"); answer(1, 200, "Cloudflare", "minor"); answer(3, 200, "OpenAI", "none");
    assert(answers_writes == 4 && checked_writes == 1 && checked_at == NOW);

    /* Affected first, each group in the list's order; a row without an answer is not healthy. */
    arrange(NOW, 300);
    assert(order[0] == 1 && order[1] == 2 && order[2] == 0 && order[3] == 3);
    reload();
    assert(count == 4 && !strcmp(services[1].name, "Cloudflare") && services[1].indicator == 1 && services[3].flags == HAS_DATA);

    /* Rounds that say the same write the round's time and nothing else,
     * and that time is every row's after a reload. */
    for (int round = 1; round <= 50; round++) {
        clock_s = NOW + 300 * round;
        reload();
        answer(0, 200, "GitHub", "none"); reload(); answer(1, 200, "Cloudflare", "minor"); reload();
        answer(2, 200, "Discord", "major"); reload(); answer(3, 200, "OpenAI", "none");
    }
    assert(answers_writes == 4 && checked_writes == 51);
    reload();
    assert(confirmed_at(&services[0]) == (uint32_t)clock_s && is_fresh(&services[0], (uint32_t)clock_s, 300));

    /* A failure keeps the last good answer and its time; the row is not fresh until a good one. */
    uint32_t last_good = (uint32_t)clock_s;
    clock_s += 300;
    answer(0, HOST_FAILED_NO_ANSWER, NULL, NULL);
    assert(answers_writes == 5 && !strcmp(services[0].name, "GitHub") && services[0].indicator == 0);
    answer(0, 200, NULL, "none"); answer(0, 503, "GitHub", "none");
    assert(answers_writes == 5);
    answer(3, 200, "OpenAI", "none");
    reload();
    assert(confirmed_at(&services[0]) == last_good && !is_fresh(&services[0], (uint32_t)clock_s, 300));
    assert(confirmed_at(&services[3]) == (uint32_t)clock_s && is_fresh(&services[3], (uint32_t)clock_s, 300));
    arrange((uint32_t)clock_s, 300);
    assert(order[0] == 0 && order[1] == 1 && order[2] == 2 && order[3] == 3);
    answer(0, 200, "GitHub", "none");
    assert(answers_writes == 6 && is_fresh(&services[0], (uint32_t)clock_s, 300));
    /* Twice the pace without a round: nothing is fresh. */
    assert(!is_fresh(&services[3], (uint32_t)clock_s + 601, 300) && is_fresh(&services[3], (uint32_t)clock_s + 600, 300));
    answer(1, 200, "Cloudflare", "unheard"); /* a word of tomorrow's */
    assert(services[1].indicator == UNKNOWN && answers_writes == 7);

    /* The list loses its first item: the rows follow their hosts, and an
     * answer for an item that is no longer there is dropped. */
    user_sets("services", "www.cloudflarestatus.com\ndiscordstatus.com\nexample.org");
    int writes = answers_writes, drawn = redraws();
    answer(3, 200, "OpenAI", "critical");
    assert(count == 3 && answers_writes == writes && redraws() == drawn);
    /* Every write so far changed the record it was of: the counts above are the host's. Later
     * rounds stand at one moment and write the same "checked" again. */
    assert(other_writes == 0);
    assert(!strcmp(services[0].name, "Cloudflare") && !strcmp(services[1].name, "Discord") && services[1].indicator == 2);
    assert(services[2].flags == 0 && services[2].name[0] == '\0' && services[2].indicator == UNKNOWN);
    answer(2, 200, "Example", "none");
    reload();
    assert(count == 3 && !strcmp(services[2].name, "Example") && !strcmp(services[0].name, "Cloudflare"));
    user_sets("services", "");
    settings();
    assert(count == 0);
    answer(0, 200, "Nobody", "none");
    assert(count == 0);

    /* The longest list: 128 items of one character. Every row survives,
     * without a name - there is no room - and the store is not over. */
    char longest[256];
    for (int at = 0; at < 255; at++) longest[at] = at % 2 ? '\n' : (char)('!' + at / 2 % 94);
    longest[255] = '\0';
    user_sets("services", longest);
    user_sets("pace", "30 min");
    settings();
    assert(count == SERVICES_MAX && name_room(count) == 0);
    for (int32_t item = 0; item < count; item++) answer(item, 200, "A name that has no room", INDICATORS[item % 5]);
    assert(pack() <= HOST_KV_VALUE_MAX && standin_store_bytes() <= HOST_KV_TOTAL_MAX);
    reload();
    assert(count == SERVICES_MAX);
    for (int32_t item = 0; item < count; item++) {
        assert(services[item].indicator == item % 5 && services[item].flags == HAS_DATA && services[item].name[0] == '\0');
        assert(confirmed_at(&services[item]) == (uint32_t)clock_s);
    }

    /* 51 hosts of four characters, the most a real name allows: names of 12 bytes, cut as said. */
    char hosts[256] = "";
    for (int at = 0; at < 51; at++) snprintf(hosts + strlen(hosts), 6, "%s%c%c.x", at ? "\n" : "", 'a' + at / 26, 'a' + at % 26);
    user_sets("services", hosts);
    settings();
    assert(count == 51 && name_room(count) == 12);
    round_of(51, "none");
    answer(7, 200, "Sервис с длинным именем", "minor");
    assert(standin_store_bytes() <= HOST_KV_TOTAL_MAX);
    reload();
    assert(!strcmp(services[50].name, "Page 50") && !strcmp(services[7].name, "Sерви...") && services[7].indicator == 1);

    /* 18 services keep whole names of 47 bytes; the record at its largest still fits. */
    hosts[0] = '\0';
    for (int at = 0; at < 18; at++) snprintf(hosts + strlen(hosts), sizeof hosts - strlen(hosts), "%sstatus%02d.io", at ? "\n" : "", at);
    user_sets("services", hosts);
    settings();
    assert(count == 18 && name_room(count) == NAME_SIZE - 1);
    for (int32_t item = 0; item < count; item++) answer(item, 200, "A status page whose name is forty-seven bytes..", "none");
    reload();
    assert(strlen(services[17].name) == 47 && !strcmp(services[17].name, "A status page whose name is forty-seven bytes.."));
    assert(standin_store_bytes() <= HOST_KV_TOTAL_MAX);

    /* Scrolling stays inside the list, and where the user is outlives an unload, not a new list. */
    assert(scrolled(0, -1, 18, 4) == 0 && scrolled(0, 1, 18, 4) == 1 && scrolled(13, 4, 18, 4) == 14);
    assert(scrolled(14, 1, 18, 4) == 14 && scrolled(2, -4, 18, 4) == 0 && scrolled(0, 1, 4, 4) == 0 && scrolled(5, 0, 3, 4) == 0);
    drawn = redraws();
    app_on_button(BTN_UP_PRESSED);
    assert(first_row == 0 && redraws() == drawn);
    app_on_button(BTN_DOWN_PRESSED); app_on_button(BTN_DOWN_LONG_PRESSED);
    assert(first_row == 5 && redraws() == drawn + 2);
    for (int press = 0; press < 20; press++) app_on_button(BTN_DOWN_PRESSED);
    assert(first_row == 14);
    app_on_button(BTN_OK_PRESSED);
    assert(standin()->polls_run[0] == 1 && first_row == 14);
    app_on_blur();
    reload();
    app_on_focus();
    assert(first_row == 14);
    user_sets("services", "a.io\nb.io");
    reload();
    app_on_focus();
    assert(first_row == 0);

    /* An update from 0.1.x: the four links are nobody's settings now and
     * leave the store; the old record is not read as rows, and the list is
     * the manifest's default of three. */
    for (int at = 0; at < STANDIN_STORE_VALUES; at++) standin()->values[at].is_stored = false;
    user_sets("page1", "https://status.example.org/api/v2/status.json");
    user_sets("page3", "");
    uint8_t old[392] = { 2 };
    kv_set("answers", 7, old, sizeof old);
    reload();
    assert(standin_value("page1", NULL) == NULL && standin_value("page3", NULL) == NULL && count == 3 && services[0].flags == 0);
    answer(0, 200, "GitHub", "none");
    reload();
    assert(!strcmp(services[0].name, "GitHub") && stored_length("answers") == RECORD_HEADER + 3 * (SLOT_FIXED + 47));

    puts("status: own rows, order, the longest list, unchanged rounds, a changed list, scrolling and the 0.1.x update passed");
    return 0;
}
