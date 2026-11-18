/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../history.h"

static const char example[] = "<?xml version=\"1.0\" encoding=\"windows-1251\"?><ValCurs ID=\"R01820\" DateRange1=\"01.10.2026\" DateRange2=\"03.10.2026\" name=\"Foreign Currency Market Dynamic\">"
    "<Record Date=\"01.10.2026\" Id=\"R01820\"><Nominal>100</Nominal><Value>51,8246</Value><VunitRate>0,518246</VunitRate></Record>"
    "<Record Date=\"03.10.2026\" Id=\"R01820\"><Nominal>100</Nominal><Value>51,6198</Value><VunitRate>0,516198</VunitRate></Record></ValCurs>";

int main(void)
{
    history_parser_t parser;
    for (size_t split = 0; split <= strlen(example); ++split) {
        history_start(&parser);
        assert(history_feed(&parser, (const uint8_t *)example, split));
        assert(history_feed(&parser, (const uint8_t *)example + split, strlen(example) - split));
        assert(history_finish(&parser));
        assert(parser.result.count == 2 && !strcmp(parser.result.id, "R01820"));
        assert(parser.result.values[0] == 5182 && parser.result.values[1] == 5161);
        assert(parser.result.as_of == history_day("2026-10-03", true));
    }
    for (size_t cut = 0; cut < strlen(example); ++cut) {
        history_start(&parser);
        history_feed(&parser, (const uint8_t *)example, cut);
        assert(!history_finish(&parser));
    }
    const char *bad[] = {
        "", "<ValCurs ID=\"R0\"></ValCurs>", "<!DOCTYPE x><ValCurs/>",
        "<ValCurs ID=\"R0\" DateRange2=\"31.02.2026\"></ValCurs>",
        "<ValCurs ID=\"R0\" DateRange2=\"01.10.2026\"><Record Date=\"01.10.2026\" Id=\"R1\"></Record></ValCurs>",
        "<ValCurs ID=\"R0\" DateRange2=\"01.10.2026\"><Record Date=\"01.10.2026\" Id=\"R0\"><Nominal>0</Nominal><Value>1</Value></Record></ValCurs>",
        "<ValCurs ID=\"R0\" DateRange2=\"01.10.2026\"><Record Date=\"01.10.2026\" Id=\"R0\"><Nominal>1,5</Nominal><Value>1</Value></Record></ValCurs>",
        "<ValCurs ID=\"R0\" DateRange2=\"01.10.2026\"><Record Date=\"01.10.2026\" Id=\"R0\"><Nominal>1</Nominal><Value>999999999999</Value></Record></ValCurs>",
    };
    for (size_t index = 0; index < sizeof bad / sizeof bad[0]; ++index) {
        history_start(&parser);
        history_feed(&parser, (const uint8_t *)bad[index], strlen(bad[index]));
        assert(!history_finish(&parser));
    }
    history_start(&parser);
    for (size_t index = 0; index < strlen(example); ++index) assert(history_feed(&parser, (const uint8_t *)example + index, 1));
    assert(history_finish(&parser));
    assert(!history_feed(&parser, (const uint8_t *)"x", 1));
    assert(!history_finish(&parser));
    assert(!history_day("2026-02-29", true));
    assert(history_day("2028-02-29", true));
    puts("Rates history: chunk boundaries, truncation, nominal normalization, dates and malformed XML passed");
    return 0;
}
