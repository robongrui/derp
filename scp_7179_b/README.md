# Episode 0.5: SCP_7179_B

A DERP-only episode.

I'd like to thank id Software for including a bookshelf texture in DOOM II.

## Playing it

It needs DERP and DOOM II (TNT and Plutonia work too). The episode's map
comes ready-made as `scp_7179_b/library.wad`, ships beside the game, and the
game loads it by itself: pick **0.5 - SCP_7179_B** in the episode menu, or
start with `-warp 0`. Other source ports can't play it: the episode lives in
DERP's code, and the WAD is only its stage.

## Credits

- **"SCP-7179"** by Calibold, from the SCP Wiki:
  <https://scpwiki.com/scp-7179>, licensed under
  [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/). The SCP
  Foundation and its concepts belong to the
  [SCP Wiki](https://scpwiki.com/) community.
- One word in this episode is spoken by **Microsoft Sam**, rendered with
  [OpenSAPI4](https://github.com/phyce/OpenSAPI4).
- The word list comes from Alan Beale's **12dicts**; see
  [words/README.md](words/README.md).

<details>
<summary>Don't open this if you don't want to be spoiled</summary>

<br>

The idea goes back to Jorge Luis Borges' short story
[*The Library of Babel*](https://en.wikipedia.org/wiki/The_Library_of_Babel)
(1941), and to the novel *A Short Stay in Hell* by Steven L. Peck, which
took it further:
[at its publisher](https://strangeviolineditions.com/catalog/a-short-stay-in-hell/),
[Steven L. Peck on Goodreads](https://www.goodreads.com/author/show/42414.Steven_L_Peck).

No characters, plot beats or text are taken from the novel or the story.
SCP_7179_B is its own take on the idea.

</details>

## Checking it

```bash
ctest --test-dir build/play -C Release -R library --output-on-failure
```

That runs the episode's own tests. The map's generator isn't published.
