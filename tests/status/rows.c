// SPDX-License-Identifier: MIT
/*
 * The application's own handlers over a store charged as Perinkpheral OS charges
 * it: which row an answer updates, the order, the record at the longest
 * list, what is written and when, a changed list, scrolling.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define HOST_IMPORT(name, ...) __VA_ARGS__
#include "../../apps/status/status.c"

uint8_t app_strings[APP_STRINGS_BYTES];

#define DEFAULT_LIST "www.githubstatus.com\nwww.cloudflarestatus.com\ndiscordstatus.com\nstatus.openai.com"
enum { KEYS = 8, NOW = 1794955920 };
static struct { char key[HOST_KV_KEY_MAX + 1]; uint8_t value[HOST_KV_VALUE_MAX]; int32_t length; bool is_set; } store[KEYS];
static int answers_writes, checked_writes, redraws, runs;
static int64_t clock_s = NOW;
static int32_t reply_item;
static const char *reply_name, *reply_indicator;

static int find(const char *key, int32_t key_length)
{
    for (int at = 0; at < KEYS; at++) {
        if (store[at].is_set && (int32_t)strlen(store[at].key) == key_length && !memcmp(store[at].key, key, (size_t)key_length)) return at;
    }
    return -1;
}
static int32_t cost(int32_t length) { return 64 + (length + 31) / 32 * 32; }
static int32_t charged(void)
{
    int32_t total = 0;
    for (int at = 0; at < KEYS; at++) { if (store[at].is_set) total += cost(store[at].length); }
    return total;
}
int32_t kv_get(const char *key, int32_t key_length, void *value, int32_t capacity)
{
    int at = find(key, key_length);
    const void *bytes = at >= 0 ? store[at].value : (const void *)DEFAULT_LIST;
    int32_t length = at >= 0 ? store[at].length : (int32_t)strlen(DEFAULT_LIST);
    if (at < 0 && (key_length != 8 || memcmp(key, "services", 8))) return HOST_ERR_NOT_FOUND;
    if (length > capacity) return HOST_ERR_NO_SPACE;
    memcpy(value, bytes, (size_t)length);
    return length;
}
int32_t kv_set(const char *key, int32_t key_length, const void *value, int32_t length)
{
    assert(key_length >= 1 && key_length <= HOST_KV_KEY_MAX && length >= 0 && length <= HOST_KV_VALUE_MAX);
    answers_writes += key_length == 7 && !memcmp(key, "answers", 7);
    checked_writes += key_length == 7 && !memcmp(key, "checked", 7);
    int at = find(key, key_length);
    if (length == 0) { if (at >= 0) store[at].is_set = false; return HOST_OK; }
    for (int free_at = 0; at < 0 && free_at < KEYS; free_at++) { if (!store[free_at].is_set) at = free_at; }
    assert(at >= 0);
    int32_t others = charged() - (store[at].is_set ? cost(store[at].length) : 0);
    if (others + cost(length) > HOST_KV_TOTAL_MAX) return HOST_ERR_NO_SPACE;
    memcpy(store[at].key, key, (size_t)key_length);
    store[at].key[key_length] = '\0';
    memcpy(store[at].value, value, (size_t)length);
    store[at].length = length;
    store[at].is_set = true;
    return HOST_OK;
}
/* What the page stores: a setting, an empty one too. */
static void user_sets(const char *key, const char *value)
{
    int at = find(key, (int32_t)strlen(key));
    for (int free_at = 0; at < 0 && free_at < KEYS; free_at++) { if (!store[free_at].is_set) at = free_at; }
    assert(at >= 0);
    strcpy(store[at].key, key);
    store[at].length = (int32_t)strlen(value);
    memcpy(store[at].value, value, strlen(value));
    store[at].is_set = true;
}
int32_t answer_item(void) { return reply_item; }
int32_t answer_value(int32_t index, char *text, int32_t capacity)
{
    const char *value = index == 0 ? reply_name : reply_indicator;
    if (value == NULL) return HOST_ERR_NOT_FOUND;
    assert((int32_t)strlen(value) <= capacity);
    memcpy(text, value, strlen(value));
    return (int32_t)strlen(value);
}
int64_t time_now(void) { return clock_s; }
void screen_redraw(int32_t flags) { (void)flags; redraws++; }
int32_t poll_run(int32_t poll) { assert(poll == 0); runs++; return HOST_OK; }
int32_t header_set_text(int32_t side, const char *text, int32_t length) { (void)side; (void)text; (void)length; return HOST_OK; }
int32_t text_fill(const char *pattern, int32_t pattern_length, const host_text_argument_t *arguments, int32_t count_of,
                  char *text, int32_t capacity)
{ (void)pattern; (void)pattern_length; (void)arguments; (void)count_of; (void)text; (void)capacity; return 0; }
static uint8_t session[HOST_SESSION_MAX];
static int32_t session_length;
int32_t session_set(const void *data, int32_t length) { memcpy(session, data, (size_t)length); session_length = length; return HOST_OK; }
int32_t session_get(void *buffer, int32_t capacity)
{
    if (session_length > capacity) return HOST_ERR_NO_SPACE;
    memcpy(buffer, session, (size_t)session_length);
    return session_length;
}

static void answer(int32_t item, int32_t http_status, const char *name, const char *indicator)
{
    reply_item = item; reply_name = name; reply_indicator = indicator;
    app_on_picked(0, http_status);
}
/* The instance is unloaded and loaded again: nothing but the store is left. */
static void reload(void)
{
    memset(services, 0xA5, sizeof services);
    memset(record, 0xA5, sizeof record);
    count = -1; checked_at = 0; list_hash = 0; first_row = 0;
    app_on_load();
}
static void round_of(int32_t services_count, const char *indicator)
{
    char name[16];
    for (int32_t item = 0; item < services_count; item++) {
        snprintf(name, sizeof name, "Page %d", item);
        answer(item, 200, name, indicator);
    }
}

int main(void)
{
    /* The default list; an answer is its item's row and nobody else's. */
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
    int writes = answers_writes, drawn = redraws;
    answer(3, 200, "OpenAI", "critical");
    assert(count == 3 && answers_writes == writes && redraws == drawn);
    assert(!strcmp(services[0].name, "Cloudflare") && !strcmp(services[1].name, "Discord") && services[1].indicator == 2);
    assert(services[2].flags == 0 && services[2].name[0] == '\0' && services[2].indicator == UNKNOWN);
    answer(2, 200, "Example", "none");
    reload();
    assert(count == 3 && !strcmp(services[2].name, "Example") && !strcmp(services[0].name, "Cloudflare"));
    user_sets("services", "");
    app_on_settings();
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
    app_on_settings();
    assert(count == SERVICES_MAX && name_room(count) == 0);
    for (int32_t item = 0; item < count; item++) answer(item, 200, "A name that has no room", INDICATORS[item % 5]);
    assert(pack() <= HOST_KV_VALUE_MAX && charged() <= HOST_KV_TOTAL_MAX);
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
    app_on_settings();
    assert(count == 51 && name_room(count) == 12);
    round_of(51, "none");
    answer(7, 200, "Sервис с длинным именем", "minor");
    assert(charged() <= HOST_KV_TOTAL_MAX);
    reload();
    assert(!strcmp(services[50].name, "Page 50") && !strcmp(services[7].name, "Sерви...") && services[7].indicator == 1);

    /* 18 services keep whole names of 47 bytes; the record at its largest still fits. */
    hosts[0] = '\0';
    for (int at = 0; at < 18; at++) snprintf(hosts + strlen(hosts), 13, "%sstatus%02d.io", at ? "\n" : "", at);
    user_sets("services", hosts);
    app_on_settings();
    assert(count == 18 && name_room(count) == NAME_SIZE - 1);
    for (int32_t item = 0; item < count; item++) answer(item, 200, "A status page whose name is forty-seven bytes..", "none");
    reload();
    assert(strlen(services[17].name) == 47 && !strcmp(services[17].name, "A status page whose name is forty-seven bytes.."));
    assert(charged() <= HOST_KV_TOTAL_MAX);

    /* Scrolling stays inside the list, and where the user is outlives an unload, not a new list. */
    assert(scrolled(0, -1, 18, 4) == 0 && scrolled(0, 1, 18, 4) == 1 && scrolled(13, 4, 18, 4) == 14);
    assert(scrolled(14, 1, 18, 4) == 14 && scrolled(2, -4, 18, 4) == 0 && scrolled(0, 1, 4, 4) == 0 && scrolled(5, 0, 3, 4) == 0);
    drawn = redraws;
    app_on_button(BTN_UP_PRESSED);
    assert(first_row == 0 && redraws == drawn);
    app_on_button(BTN_DOWN_PRESSED); app_on_button(BTN_DOWN_LONG_PRESSED);
    assert(first_row == 5 && redraws == drawn + 2);
    for (int press = 0; press < 20; press++) app_on_button(BTN_DOWN_PRESSED);
    assert(first_row == 14);
    app_on_button(BTN_OK_PRESSED);
    assert(runs == 1 && first_row == 14);
    app_on_blur();
    reload();
    app_on_focus();
    assert(first_row == 14);
    user_sets("services", "a.io\nb.io");
    reload();
    app_on_focus();
    assert(first_row == 0);

    /* An update from 0.1.x: the four links are nobody's settings now and
     * leave the store; the old record is not read as rows. */
    memset(store, 0, sizeof store);
    user_sets("page1", "https://status.example.org/api/v2/status.json");
    user_sets("page3", "");
    uint8_t old[392] = { 2 };
    kv_set("answers", 7, old, sizeof old);
    reload();
    assert(find("page1", 5) < 0 && find("page3", 5) < 0 && count == 4 && services[0].flags == 0);
    answer(0, 200, "GitHub", "none");
    reload();
    assert(!strcmp(services[0].name, "GitHub") && store[find("answers", 7)].length == RECORD_HEADER + 4 * (SLOT_FIXED + 47));

    puts("status: own rows, order, the longest list, unchanged rounds, a changed list, scrolling and the 0.1.x update passed");
    return 0;
}
