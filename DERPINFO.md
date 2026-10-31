# DERPINFO: settings for your map

DERP works out a map's look from the WAD itself: where the sun stands, which
way the wind blows, whether it rains. With a `DERPINFO` lump you can set these
yourself, per map: the weather and the wind, the sun, a distance fog, wall
mirrors, and DERP's Phobos physics (low gravity, vacuum outside).

Other ports ignore the lump, so your WAD keeps working everywhere.

## Adding it to your WAD

No special editor needed. Build your map as usual, then:

1. Open the WAD in [SLADE](https://slade.mancubus.net/).
2. Make a new entry, name it `DERPINFO`.
3. Type your settings (below) into it as plain text and save.

## What it looks like

```
map E1M1
{
    weather = snow
    weatheramount = 0.7
    wind = 135
    windamount = 0.8
    sun = 200, 50
}

map MAP07
{
    phobos = true
    gravity = 0
}

map MAP12
{
    weather = fog
    sun = none
    farfog = 256, 1536
    farfogcolor = 2E271F
    mirror = 8, 128, 8, 192, 16, 112
}
```

One `map` block per map, named like the map's lump (`E1M1`, `MAP07`). Leave
out anything you don't want to set: DERP works it out as before. `//` and
`/* */` comments are fine. Each setting goes on one line.

## Settings

| Key | Values | What it does |
|---|---|---|
| `weather` | `none`, `rain`, `snow`, `nukage`, `sand`, `ash`, `fog`, `dryice` | What falls under the sky. `sand` is a sandstorm blown along the ground; `ash` falls as from an eruption, dark grey with a few glowing embers. `fog` lies instead: it pools in the low ground outside and the drafts carry it into the rooms; `dryice` lies as on a colder world: a low white layer outside that pours off ledges and fills pits, under a clear sky; indoors it only streams along the drafts. `nukage` is rain in your nukage's colour that hurts everything alive under the open sky, or where it blows in at a broken window, 2 every 32 tics, gentler than a nukage floor (a radiation suit protects the player). It slowly eats the monsters away where its drops land, and melts their corpses away. |
| `weatheramount` | `0` to `1` | How hard it rains, or how thick the snow, sand, ash or fog. `1` is a downpour. |
| `wind` | degrees | Where the wind blows *to*, as your editor shows thing angles: `0` east, `90` north, `180` west, `270` south. Rain and snow slant with it, and broken windows facing it let weather in. |
| `windamount` | `0` to `1` | How hard the wind blows: `0` still air, `0.5` a steady breeze (about what DERP picks when you leave it out), `1` a gale. In still air rain and snow fall straight down, nothing blows in at a window and the air in the buildings doesn't move; the harder it blows, the flatter they slant, the further they drift in under roofs and through broken windows, and the stronger the drafts through the rooms (and the colder they get in snow). |
| `sun` | angle, height | Where the sun stands: the angle as above, then its height over the horizon in degrees (`1` to `90`). Shadows, sunbeams and the sun in the sky follow it. The sky texture still gives its colour and strength. |
| `sun` | `none` or `hidden` | No sun on this map: no sunlight, no sunbeams. |
| `phobos` | `true`, `false` | DERP's Phobos physics: very low gravity, things slide and drift, vacuum outside the sealed rooms, broken windows and open doors vent air, airlocks, mag boots. `false` keeps it off even where DERP would use it (DOOM's first episode). |
| `gravity` | `0` and up, in g | With `phobos = true`: how hard things fall, in Earth gravities. `0` weightless, `0.00058` Phobos (the default), `0.165` the Moon, `0.38` Mars. Doom's own fall is about `3.9`, the most it takes. |
| `farfog` | near, far | A distance fog as in Turok: everything is clear up to *near* map units away, fades into the fog colour, and is solid from *far* on; behind it all is the fog colour, and what is wholly past *far* may not be drawn. `none` turns it off. |
| `farfogcolor` | `RRGGBB` | The far fog's colour in hex, as `2E271F` (dark dust). Without it, near black. |
| `mirror` | x1, y1, x2, y2, bottom, top | A wall mirror: silvered glass along a wall from (x1, y1) to (x2, y2), facing that line's right side as a linedef's front does, from *bottom* to *top* above the map's zero. It shows the world and everyone in it, the player included (with "Window glass" and "Glass reflections" on), while it is near and in view. Each `mirror` line adds one, up to 8; `none` drops those before it. |
| `library` | `true`, `false` | Reserved for episode 0.5, SCP_7179_B. Leave it out. |

## Notes

- Switches (`phobos`, `library`) take `true`/`false`, `on`/`off`,
  `yes`/`no` or `1`/`0`. Keys, values and map names don't care about case.
- Players choose in DERP's settings (F4) whether weather follows the map:
  *Weather: Map's own* (the default) uses yours, *Clear*, *Rain*, *Snow*,
  *Nukage rain*, *Sand*, *Ash*, *Fog* or *Dry ice* override it. They can
  also pick the *Time of day*, *Temperature*, *Wind* and *Wind from*; where
  your weather, wind or sun clashes with what they picked, yours gives way
  to the nearest that fits (snow above freezing falls as rain, fog in the
  heat clears). `nukage` is kept as you set it.
- Phobos maps have no weather: there is no air to carry it.
- Nukage rain is a rule of the game, so like Phobos it is off in demos and
  multiplayer; there it falls as plain rain.
- Phobos physics never runs in demos or multiplayer.
- If several loaded WADs set the same map, the one loaded last wins, setting
  by setting.
- Mistakes (a misspelled key, a value out of range) are written to DERP's log
  (`game.log`) with the line number, and skipped. The game still runs.
