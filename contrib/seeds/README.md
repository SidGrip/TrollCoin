# Seeds

Utility to generate the seed node list that is compiled into the client
(see [src/chainparamsseeds.h](/src/chainparamsseeds.h) and other utilities in [contrib/seeds](/contrib/seeds)).

TrollCoin seed nodes are served by `dnsfeed.trollcoin.com`.

Be sure to update `PATTERN_AGENT` in `makeseeds.py` to include the current version,
and remove old versions as necessary (at a minimum when GetDesirableServiceFlags
changes its default return value, as those are the services which seeds are added
to addrman with).

To regenerate the compiled-in list from `nodes_main.txt` and `nodes_test.txt`,
run the following command from the `/contrib/seeds` directory:

```
python3 generate-seeds.py . > ../../src/chainparamsseeds.h
```
