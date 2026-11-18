# GitHub activity

Set Login and a read-only GitHub token in the app settings. Requests go directly to GitHub's
GraphQL API with a bearer token and User-Agent. The app refreshes on opening, on OK, when
the network returns, after an app update and every six hours by an alarm. Missing or invalid
credentials show Not set up. A failed answer keeps the previous complete calendar for that login.

The calendar uses GitHub's contribution levels, including its choice of contribution
counts and dates. A streaming selector follows the nested weeks and day objects, while
the SDK validates the complete JSON document. It accepts arbitrary chunk boundaries and
property order. Only three bits per day, the first date, day count and total survive in
the store. Complete responses with 1–378 consecutive dates are accepted, allowing for
GitHub's calendar-week padding. No contribution counts or response body are retained.

The grid has 53 Sunday-first columns of 6 px cells with 1 px gaps. Empty days leave paper;
levels 1-4 use Perinkpheral OS colours 4, 8, 12 and 16. Month initials use the current
language. The last returned week occupies the rightmost column. A rare 54-week span crops only the earliest
partial week from the drawing, keeping the complete year and total in storage. The streak
requires contributions today and walks backward through consecutive local calendar dates;
it is zero when today's date is absent or empty. Unknown time also gives zero.

`tests/github/calendar.json` is a synthetic, realistic 30 KB GraphQL response containing
366 days. `tests/check-logic.sh` checks split responses down to one byte, packed levels,
dates, streaks and invalid or truncated data with sanitizers.
