// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../rates.c"
#include "../history.c"
#include "standin.h"

enum { TEXT_BYTES = 16384 };
static int answered_requests; /* queued requests the test has looked at and taken off the queue */

static int writes(void) { return standin()->file_writes; }
static int requests(void) { return answered_requests + standin()->request_count; }
static void fail_writes(bool fails) { standin()->refuse_writes = fails ? HOST_ERR_FAILED : HOST_OK; }

/* Every request the application made is a GET with its own tag and an address that fits;
 * the queue is emptied as the device's is when it has asked. */
static void take_requests(void)
{
    standin_t *host = standin();
    for (int32_t index = 0; index < host->request_count; index++) {
        assert(host->requests[index].tag >= HOST_REQUEST_FIRST && host->requests[index].method == HOST_HTTP_GET);
        assert(strlen(host->requests[index].url) < 192);
    }
    answered_requests += host->request_count;
    host->request_count = 0;
    /* No write was refused by the host's limits, and every request was taken. */
    assert(host->refused_what[0] == '\0' && host->sends == answered_requests && answered_requests < 16);
}

static void deliver(int request, const char *body, int status)
{
    take_requests();
    standin_data(request, -1, status, body, (int32_t)strlen(body), 17, STANDIN_WHOLE, 0);
}

/* The body the application's mocks (test/check.json) keep for the address with this ending. */
static char *mock_body(const char *ending)
{
    char *text = calloc(1, TEXT_BYTES); assert(text);
    for (int32_t index = 0; index < standin_subject.answer_count; index++) {
        const standin_answer_t *answer = &standin_subject.answers[index];
        size_t length = strlen(answer->address), ending_length = strlen(ending);
        if (length < ending_length || strcmp(answer->address + length - ending_length, ending)) continue;
        if (answer->file != NULL) {
            FILE *input = fopen(answer->file, "rb"); assert(input);
            size_t size = fread(text, 1, TEXT_BYTES - 1, input); assert(size && !ferror(input)); fclose(input);
        } else {
            assert(answer->body != NULL && strlen(answer->body) < TEXT_BYTES);
            strcpy(text, answer->body);
        }
        return text;
    }
    assert(!"no such mock");
    return text;
}
int main(void)
{
    char *daily = mock_body("/daily_json.js"), *usd = mock_body("VAL_NM_RQ=R01235"), *jpy = mock_body("VAL_NM_RQ=R01820");
    standin_reset();
    standin_handlers.on_data = app_on_data;
    standin_set("codes", "USD\nJPY");
    app_on_load();
    app_on_focus();
    assert(standin()->polls_run[0] == 1);
    deliver(0, daily, 200);
    assert(state.has_data && currency_count() == 2 && requests() == 2);
    assert(strstr(standin()->requests[0].url, "VAL_NM_RQ=R01235"));
    assert(strstr(standin()->requests[1].url, "VAL_NM_RQ=R01820"));
    assert(rate_for(1)->value / rate_for(1)->nominal == 5182);
    deliver(HOST_REQUEST_FIRST, usd, 200);
    assert(chart.history.count > 1 && !strcmp(chart.code, "USD"));
    int previous_writes = writes();
    deliver(HOST_REQUEST_FIRST, usd, 200);
    assert(writes() == previous_writes);
    state_t saved_state = state;
    deliver(0, "{\"Date\":\"bad\"}", 200);
    assert(!memcmp(&saved_state, &state, sizeof state));
    standin_set("codes", "JPY\nUSD");
    app_on_settings();
    assert(chart.history.count == 0);
    /* An already queued USD answer follows its ID, never its old slot. */
    deliver(HOST_REQUEST_FIRST, usd, 200);
    assert(chart.history.count == 0);
    cached_history_t record;
    read_history(1, &record);
    assert(!strcmp(record.code, "USD") && record.history.count > 1);
    fail_writes(true);
    deliver(HOST_REQUEST_FIRST + 1, jpy, 200);
    assert(chart.history.count == 0);
    fail_writes(false);
    deliver(HOST_REQUEST_FIRST + 1, jpy, 200);
    assert(!strcmp(chart.code, "JPY") && chart.history.count > 1);
    assert(chart.history.values[chart.history.count - 1] < 10000);
    previous_writes = writes();
    request_histories();
    assert(writes() == previous_writes);
    cached_history_t before = chart;
    deliver(HOST_REQUEST_FIRST, "<ValCurs ID=\"R01820\">broken", 200);
    assert(!memcmp(&before, &chart, sizeof chart));
    app_on_button(BTN_DOWN_PRESSED);
    assert(navigation.index == 1 && !strcmp(chart.code, "USD"));
    app_on_button(BTN_OK_PRESSED);
    assert(navigation.rotating);
    app_on_timer(ROTATE_TIMER);
    assert(navigation.index == 0 && !strcmp(chart.code, "JPY"));
    size_t daily_length = strlen(daily);
    assert(daily_length > 2 && !strcmp(daily + daily_length - 2, "}}"));
    strcpy(daily + daily_length - 2, ",\"CAD\":{\"ID\":\"R01350\",\"Value\":45.125,\"Nominal\":1}}}");
    const char *lists[] = { "USD", "USD\nEUR\nCNY", "USD\nEUR\nCNY\nJPY",
        "USD\nEUR\nCNY\nJPY\nGBP\nCHF\nAUD\nCAD" };
    const int picker_counts[] = { 1, 1, 2, 3 };
    for (int test = 0; test < 4; test++) {
        standin_set("codes", lists[test]);
        begin_reading();
        assert(active_pickers == picker_counts[test]);
        for (size_t offset = 0; offset < strlen(daily); offset++) {
            for (int group = 0; group < active_pickers; group++)
                assert(json_pick_feed(&pickers[group], daily + offset, 1));
        }
        assert(take_response());
        assert(state.rates[0].nominal == 1 && state.rates[0].value == 824529);
        if (test > 0) assert(state.rates[2].nominal == 1 && state.rates[2].value == 123419);
        if (test > 1) assert(state.rates[3].nominal == 100 && state.rates[3].value == 518246);
        if (test == 3) {
            assert(state.rates[7].nominal == 1 && state.rates[7].value == 451250);
            assert(state.rates[6].nominal == 0); /* Missing codes stay missing. */
        }
    }
    standin_set("codes", "");
    begin_reading();
    assert(active_pickers == 1 && count == 0);
    assert(json_pick_feed(&pickers[0], daily, strlen(daily)) && take_response());
    take_requests();
    free(daily); free(usd); free(jpy);
    puts("Rates lifecycle: cached quotes, ID-safe reorder, write failures, history reuse, navigation and rotation passed");
    return 0;
}
