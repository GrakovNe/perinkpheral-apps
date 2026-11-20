# Service status

Atlassian Statuspage summaries of the services in the Services list, initially GitHub,
Discord and OpenAI (Cloudflare's page was left out of the defaults on 2026-11-21: the device
could not open a connection to it within the request's time, every round). An item is the host of a status page, `www.githubstatus.com`:
the application asks `https://<host>/api/v2/status.json`. A scheme, a port or a path in an
item is not a host and the row stays without data. About 12 hosts of 20 characters fit the
list's 255 bytes; whole addresses would fit 5.

Perinkpheral OS asks the services one at a time, each round at the Refresh pace (1, 5, 15 or
30 minutes; 5 by default), and an answer updates its own row. OK asks every service again.
Affected services come first, with their number in the header. Maintenance counts as
affected. Unknown data has its own state and does not count as a confirmed outage. UP and
DOWN scroll the rows, held a page at a time; the scroll bar is Perinkpheral OS's.

## Store

Failed or malformed answers preserve the previous good values. The rows are one store value,
`answers`: 8 bytes, then for every service 2 bytes of its host's hash, 4 of the time, 1 of
the state and its name. A row follows its host when the list is edited. The name has what
1016 bytes divided among the services leave after those 7: 47 bytes up to 18 services, 12
at 51, none at 128, where the row shows the host. A longer name is cut with "...".

The value is written when a row's state or name changes, not at every answer. The time a
round ended is a value of its own, `checked`, written once a round, and is the time of
every row whose last answer was good.

At their largest, as stored: the list 320 bytes, the pace 96, `answers` 1088 (128 services
of one character, the most the list holds, take 904 of its 1024) and `checked` 96: 1600 of
2048.

## Update from 0.1.x

The four link settings are gone and the list starts from its default. Links stored by a
user are removed from the store at the first start; they are not converted.
