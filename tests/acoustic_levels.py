#!/usr/bin/env python3
"""Test levels for the room acoustics (platform/audio/room.c): a PWAD of
plain rectangle maps, and the spots on each where doom-room-probe measures
what the reverb hears.

  E1M1 tunnel-water    128 x 128 x 2048, stone walls and ceiling, water floor
  E1M2 tunnel-stone    the same, stone floor
  E1M3 box-water       512 x 512 x 128: the water tunnel's volume, its materials
  E1M4 box-stone       the same, stone floor
  E1M5 l-room          two arms 256 wide, 1024 and 768 long, meeting in a corner
  E1M6 hall            1024 x 768 x 256, stone
  E1M7 tunnel-openings the stone tunnel with alcoves and a doorway to a side room
  E1M8 tunnel-hall     a stone tunnel ending in a 1024 x 1024 x 256 hall
  E1M9 canyon          the stone tunnel 256 high, open to the sky

  py tests/acoustic_levels.py write <out.wad> [--doom2]
      the PWAD (--doom2 names the maps MAP01-MAP09)
  py tests/acoustic_levels.py probe <probe exe> <iwad> <out dir> [--doom2] [maps]
      measures every spot (and the walk past the side openings) into
      <out dir>/<map>-<spot>.room, a summary to the console
  py tests/acoustic_levels.py check <probe exe> <iwad> <out dir> [--doom2]
      probes and checks what tunnels should sound like (the ctest)
  py tests/acoustic_levels.py map02 <probe exe> <doom2.wad> <out dir>
      measures DOOM II's MAP02 at the water channel round the middle building
      (128 wide, 96 high, roofed) and its two lower crawlways

The game runs hidden through doom-room-probe with its own config in the out
dir, never the player's. No id data: plain geometry and stock texture names
found in both IWADs."""
import math, os, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ragdoll_arena import build, name8

STONE, WATER, ROCK, SKY = 'STONE2', 'FWATER1', 'FLOOR4_8', 'F_SKY1'
LIGHT = 160

def room(x0, y0, x1, y1, ceiling=128, floor=ROCK, top=ROCK):
    return (x0, y0, x1, y1, 0, ceiling, LIGHT, floor, top, STONE)

def tunnel(floor=ROCK, x=0, ceiling=128, top=ROCK):
    """128 wide, 2048 long, north-south from (x, 0), in two halves: a BSP needs a node."""
    return [room(x, 0, x + 128, 1024, ceiling, floor, top), room(x, 1024, x + 128, 2048, ceiling, floor, top)]

# Each map: name, rectangles, the player's start, the spots (name, x, y, facing in degrees),
# and walks (name, x0, y0, x1, y1, steps, facing). 90 faces north, along the tunnels.
LEVELS = [
    ('tunnel-water', tunnel(WATER), (64, 512, 90),
     [('middle', 64, 1000, 90), ('middle-side', 64, 1000, 0), ('quarter', 64, 512, 90), ('end', 64, 160, 90)], []),
    ('tunnel-stone', tunnel(), (64, 512, 90),
     [('middle', 64, 1000, 90), ('middle-side', 64, 1000, 0), ('end', 64, 160, 90)], []),
    ('box-water', [room(0, 0, 256, 512, floor=WATER), room(256, 0, 512, 512, floor=WATER)], (200, 256, 90),
     [('middle', 200, 256, 90), ('corner', 100, 100, 45)], []),
    ('box-stone', [room(0, 0, 256, 512), room(256, 0, 512, 512)], (200, 256, 90),
     [('middle', 200, 256, 90), ('corner', 100, 100, 45)], []),
    ('l-room', [room(0, 0, 256, 1024), room(256, 0, 1024, 256)], (128, 128, 45),
     [('corner', 128, 128, 45), ('arm', 128, 700, 90), ('arm-east', 700, 128, 0)], []),
    ('hall', [room(0, 0, 512, 768, 256), room(512, 0, 1024, 768, 256)], (500, 384, 90),
     [('middle', 500, 384, 90), ('near-wall', 100, 384, 0)], []),
    ('tunnel-openings', tunnel() + [room(128, 384, 192, 448), room(-64, 896, 0, 960), room(128, 1600, 192, 1664),
                                    room(-32, 1376, 0, 1440), room(-352, 1248, -32, 1568)], (64, 64, 90),
     [('alcove', 64, 416, 90), ('doorway', 64, 1408, 90), ('middle', 64, 1150, 90)],
     [('walk', 64, 64, 64, 1984, 60, 90)]),
    ('tunnel-hall', tunnel(x=448) + [room(0, 2048, 512, 3072, 256), room(512, 2048, 1024, 3072, 256)], (512, 512, 90),
     [('middle', 512, 1000, 90), ('near-hall', 512, 1800, 90), ('mouth', 512, 2000, 90), ('in-hall', 512, 2400, 90)], []),
    ('canyon', tunnel(ceiling=256, top=SKY), (64, 512, 90),
     [('middle', 64, 1000, 90)], []),
]

# DOOM II MAP02: the water channel round the middle building, and the crawlways south-west.
MAP02_SPOTS = [('ring-west', 512, 1500, 90), ('ring-west-side', 512, 1500, 0), ('ring-east', 1536, 1400, 90),
               ('ring-south', 800, 1056, 0), ('ring-north', 900, 1816, 0), ('ring-corner', 528, 1728, 45),
               ('crawl-west', 232, 896, 90), ('crawl-south', 424, 608, 0)]

def write(path, doom2):
    lumps = []
    for i, (name, rects, start, spots, walks) in enumerate(LEVELS):
        lumps.append(('MAP%02d' % (i + 1) if doom2 else 'E1M%d' % (i + 1), b''))
        lumps += build(rects, None, start=start)
    body, directory, offset = b'', b'', 12
    for name, data in lumps:
        directory += struct.pack('<2i', offset, len(data)) + name8(name)
        body += data; offset += len(data)
    with open(path, 'wb') as out:
        out.write(b'PWAD' + struct.pack('<2i', len(lumps), offset) + body + directory)

def read_room(path):
    """A .room file as a dict: single values as floats, taps as lists of rows."""
    room = {'tap': []}
    for line in open(path):
        words = line.split()
        if not words:
            continue
        if words[0] == 'tap':
            room['tap'].append([float(w) for w in words[2:]])
        elif len(words) == 2:
            try:
                room[words[0]] = float(words[1])
            except ValueError:
                room[words[0]] = words[1]
    return room

def probe(exe, iwad, out, doom2=False, only=None):
    """Measures the levels' spots; returns {(map, spot): room} and {(map, walk): [rooms]}."""
    out = os.path.abspath(out)
    os.makedirs(out, exist_ok=True)
    wad = os.path.join(out, 'acoustic-levels.wad')
    write(wad, doom2)
    rooms, walks = {}, {}
    for i, (name, rects, start, spots, walk_list) in enumerate(LEVELS):
        if only and name not in only:
            continue
        command = [os.path.abspath(exe), os.path.abspath(iwad), '-file', wad, '-out', out, '-name', name]
        command += ['-warp', str(i + 1)] if doom2 else ['-warp', '1', str(i + 1)]
        for spot, x, y, facing in spots:
            command += ['-at', '%s:%g,%g,%g' % (spot, x, y, facing)]
        for walk, x0, y0, x1, y1, steps, facing in walk_list:
            command += ['-walk', '%s:%g,%g,%g,%g,%d,%g' % (walk, x0, y0, x1, y1, steps, facing)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise SystemExit('%s: probe failed (%d)\n%s%s' % (name, result.returncode, result.stdout, result.stderr))
        print(''.join(l + '\n' for l in result.stdout.splitlines() if l.startswith(('spot', 'walk'))), end='')
        for spot, *_ in spots:
            rooms[(name, spot)] = read_room(os.path.join(out, '%s-%s.room' % (name, spot)))
        for walk, x0, y0, x1, y1, steps, facing in walk_list:
            walks[(name, walk)] = [read_room(os.path.join(out, '%s-%s-%02d.room' % (name, walk, s)))
                                   for s in range(steps)]
    return rooms, walks

def map02(exe, iwad, out):
    out = os.path.abspath(out)
    os.makedirs(out, exist_ok=True)
    command = [os.path.abspath(exe), os.path.abspath(iwad), '-warp', '2', '-out', out, '-name', 'map02']
    for spot, x, y, facing in MAP02_SPOTS:
        command += ['-at', '%s:%g,%g,%g' % (spot, x, y, facing)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit('MAP02: probe failed (%d)\n%s%s' % (result.returncode, result.stdout, result.stderr))
    print(''.join(l + '\n' for l in result.stdout.splitlines() if l.startswith('spot')), end='')

# Rooms that are no tunnel, as room.c measured them before tunnels were told
# apart (DOOM's and DOOM II's STONE2 and FLOOR4_8): decay, damping, scatter,
# late, then each echo's delay and gain. Tunnels must leave them as they were.
UNCHANGED = {
    ('box-water', 'middle'): (1.455754, 0.399410, 0.447639, 0.831790,
        [(0.062760, 0.153714), (0.056450, 0.162056), (0.051495, 0.169270), (0.044102, 0.181313), (0.040231, 0.188329),
         (0.052749, 0.167384), (0.051495, 0.169270), (0.065098, 0.150836), (0.016262, 0.176904)]),
    ('box-stone', 'corner'): (1.100932, 0.450000, 0.650000, 0.832666,
        [(0.082875, 0.132044), (0.090850, 0.125055), (0.059504, 0.157908), (0.022051, 0.230152), (0.020115, 0.235725),
         (0.022051, 0.230152), (0.020115, 0.235725), (0.046860, 0.176625), (0.016262, 0.176904)]),
    ('hall', 'middle'): (2.102411, 0.450000, 0.650000, 1.000000,
        [(0.105404, 0.114038), (0.084675, 0.130399), (0.077243, 0.137470), (0.102589, 0.116016), (0.100576, 0.117471),
         (0.084675, 0.130399), (0.077243, 0.137470), (0.106295, 0.113427), (0.040187, 0.134579)]),
    ('l-room', 'corner'): (1.029192, 0.450000, 0.650000, 0.905038,
        [(0.098924, 0.118694), (0.065514, 0.150336), (0.098924, 0.118694), (0.028225, 0.214011), (0.025748, 0.220208),
         (0.028225, 0.214011), (0.025748, 0.220208), (0.065514, 0.150336), (0.016262, 0.176904)]),
}

def check(exe, iwad, out, doom2=False):
    rooms, walks = probe(exe, iwad, out, doom2)
    failures = []
    def expect(ok, what):
        print('%s %s' % ('ok  ' if ok else 'FAIL', what))
        if not ok:
            failures.append(what)
    water, stone = rooms[('tunnel-water', 'middle')], rooms[('tunnel-stone', 'middle')]
    box = rooms[('box-water', 'middle')]
    # Telling tunnels apart.
    expect(water['tunnel'] > 0.95 and stone['tunnel'] > 0.95, 'the test tunnels are tunnels')
    for key in (('box-water', 'middle'), ('box-water', 'corner'), ('box-stone', 'middle'), ('box-stone', 'corner'),
                ('hall', 'middle'), ('hall', 'near-wall'), ('l-room', 'corner')):
        expect(rooms[key]['tunnel'] == 0, '%s %s is no tunnel' % key)
    expect(rooms[('l-room', 'arm')]['tunnel'] < 0.3, 'an arm of the L-shaped room is hardly one')
    expect(rooms[('canyon', 'middle')]['tunnel'] < 0.05, 'a canyon open to the sky is none')
    walk = [r['tunnel'] for r in walks[('tunnel-openings', 'walk')]]
    expect(min(walk) > 0.95 and max(abs(a - b) for a, b in zip(walk, walk[1:])) < 0.05,
           'walking past alcoves and a side doorway keeps the tunnel steady')
    # The tail.
    expect(water['decay'] > stone['decay'] > box['decay'], 'water tunnel rings longer than stone, stone than the same air as a box')
    expect(water['late'] >= box['late'], 'a tunnel tail is as loud as the box\'s')
    # The echoes along the tunnel (it runs north: echoes 1, 2, 5 and 6) below those off its sides.
    along = [water['tap'][k][1] for k in (1, 2, 5, 6)]
    side = [water['tap'][k][1] for k in (0, 3, 4, 7)]
    expect(max(along) < 0.5 * min(side), 'echoes along the tunnel are weaker than those off its sides')
    expect(min(water['tap'][k][2] for k in (1, 2, 5, 6)) > 0.95, 'and smeared')
    # The flutter.
    expect(water['flutter'] > stone['flutter'] > 0, 'it flutters, more over water than stone')
    expect(abs(water['flutter_delay'] - 2 * 128 / 10700) < 1e-4, 'at the round trip across the tunnel')
    expect(box['flutter'] == 0, 'a box doesn\'t flutter')
    # The space beyond.
    for key in (('tunnel-water', 'middle'), ('tunnel-water', 'end'), ('tunnel-stone', 'end')):
        expect(rooms[key]['beyond_coupling'] == 0, '%s %s: its own end is no space beyond' % key)
    expect(rooms[('tunnel-hall', 'near-hall')]['beyond_coupling'] > 0.5, 'a hall at the tunnel\'s end still is')
    # Rooms that are no tunnel, as before.
    for key, (decay, damping, scatter, late, taps) in UNCHANGED.items():
        r = rooms[key]
        same = all(abs(a - b) < 2e-5 for a, b in ((r['decay'], decay), (r['damping'], damping), (r['scatter'], scatter),
                                                (r['late'], late)))
        same = same and all(abs(r['tap'][k][0] - d) < 2e-5 and abs(r['tap'][k][1] - g) < 2e-5 for k, (d, g) in enumerate(taps))
        expect(same, '%s %s sounds as before' % key)
    if failures:
        raise SystemExit('%d checks failed' % len(failures))

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    doom2 = '--doom2' in sys.argv
    if len(args) == 2 and args[0] == 'write':
        write(args[1], doom2)
        print('%s: %d maps' % (args[1], len(LEVELS)))
    elif len(args) >= 4 and args[0] == 'probe':
        probe(args[1], args[2], args[3], doom2, args[4:])
    elif len(args) == 4 and args[0] == 'map02':
        map02(args[1], args[2], args[3])
    elif len(args) == 4 and args[0] == 'check':
        check(args[1], args[2], args[3], doom2)
    else:
        raise SystemExit(__doc__)

if __name__ == '__main__':
    main()
