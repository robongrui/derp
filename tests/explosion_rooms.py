"""Explosion test rooms: a PWAD of small rooms (ragdoll_arena.py's builder), and hidden renders of a blast in
each as contact sheets, to judge whether the fire and smoke take the room's shape (PLAN-explosions.md).

    py tests/explosion_rooms.py                    every case, into output/explosion-rooms/
    py tests/explosion_rooms.py wall-front low     only these cases
    py tests/explosion_rooms.py --list             the cases
    options: --monsters KIND (a row of four in each room, e.g. imp: the blast's kills)  --mode N (F4 Explosions: 1 Doom-like, 2 Physical; default 2)  --kind N (DERP_EXPLODE's kind: 4 the
             game's rocket, 2 a barrel's mix; default by case)  --every N  --frames N  --out DIR

The rooms (E1M1 to E1M9, the player's start at 0,-200 facing north):
  1 wall      a plain room 192 high, its north wall at y 160
  2 corridor  96 wide, 128 high, running north to an end wall at y 512
  3 low       a wide room 72 high
  4 doorway   two rooms 192 high, a doorway 64 wide and 72 high in the 16-thick wall between them (y 160 to 176)
  5 tall      a room 512 high
  6 pillar    a 64 x 64 pillar at 0,92 in a room 192 high
  7 alcove    a niche in the north wall: 96 wide, 64 deep, its floor 24 up, 80 high
  8 ledge     the floor drops 96 at y 160
  9 yard      open sky, a wall at y 160
Nothing here is the game's own content; the sheets are for the eye. No sound, no monsters, copies of your settings.
"""
import os, re, shutil, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ragdoll_arena as arena

def halves(y0, y1, floor, ceiling, *more):
    return [(-384, y0, 0, y1, floor, ceiling) + more, (0, y0, 384, y1, floor, ceiling) + more]

def rooms():
    pillar = []
    xs, ys = [-384, -32, 32, 384], [-448, 60, 124, 400]
    for x0, x1 in zip(xs, xs[1:]):
        for y0, y1 in zip(ys, ys[1:]):
            closed = (x0, y0) == (-32, 60)
            pillar.append((x0, y0, x1, y1, 0, 0 if closed else 192))
    return [
        halves(-448, 160, 0, 192),
        [(-48, -448, 0, 512, 0, 128), (0, -448, 48, 512, 0, 128)],
        halves(-448, 384, 0, 72),
        [(-384, -448, 384, 160, 0, 192), (-384, 160, -32, 176, 0, 0), (-32, 160, 32, 176, 0, 72), (32, 160, 384, 176, 0, 0),
         (-384, 176, 384, 768, 0, 192)],
        halves(-448, 160, 0, 512),
        pillar,
        [(-384, -448, 384, 160, 0, 192), (-384, 160, -48, 224, 0, 0), (-48, 160, 48, 224, 24, 104), (48, 160, 384, 224, 0, 0)],
        [(-384, -448, 384, 160, 0, 256), (-384, 160, 384, 768, -96, 256)],
        halves(-448, 160, 0, 256, 255, arena.FLOOR, 'F_SKY1'),
    ]

# name: (map, blast x, y, kind, view x, y, angle, what to look for)
CASES = {
    'wall-front':     (1, 0, 150, 4, 0, -150, 90, 'a rocket on a flat wall, seen head on: the fire should flatten on the wall and spread over it, not stand as a ball or a box'),
    'wall-side':      (1, 0, 150, 4, -260, 120, 8, 'the same from the side, along the wall: half a ball bulging out from the wall, nothing behind the wall'),
    'wall-barrel':    (1, 0, 130, 2, -260, 100, 8, 'a barrel beside the wall, from the side: the fire climbs the wall'),
    'corridor-along': (2, 0, 100, 4, 0, -200, 90, 'a rocket in a corridor 96 wide, seen along it: the fire fills it wall to wall and runs toward the eye and away'),
    'corridor-end':   (2, 0, 500, 4, 0, 200, 90, 'a rocket on the corridor\'s end wall: fire comes back down the corridor'),
    'low-front':      (3, 0, 100, 4, 0, -200, 90, 'a rocket in a room 72 high: flat under the ceiling, spreading sideways'),
    'low-barrel':     (3, 0, 100, 2, 0, -200, 90, 'a barrel in a room 72 high: a sheet of fire under the ceiling, wider than tall'),
    'doorway-near':   (4, 0, 120, 2, -220, 60, 20, 'a barrel 40 before a doorway, from the side: a tongue of fire and smoke through the doorway, the wall beside it bare'),
    'doorway-beyond': (4, 0, 230, 4, 0, -150, 90, 'a rocket in the far room just behind the doorway, seen from the near room: fire only through the opening, none through the wall'),
    'tall-floor':     (5, 0, 60, 2, 0, -260, 90, 'a barrel in a room 512 high: a mushroom that rises free, no flat top'),
    'tall-wall':      (5, 0, 150, 4, -260, 120, 8, 'a rocket on a wall in a tall room, from the side: up the wall, no flat top'),
    'pillar-behind':  (6, 0, 170, 2, 0, -200, 90, 'a barrel straight behind a pillar: the pillar hides its middle, fire and smoke come round both sides'),
    'pillar-beside':  (6, 70, 92, 4, 0, -200, 90, 'a rocket on the pillar\'s east face: fire on that face only, the pillar itself not covered'),
    'alcove-in':      (7, 0, 200, 4, 0, -150, 90, 'a rocket into a niche 96 wide and 80 high: the niche full, fire and smoke pouring out of it into the room'),
    'alcove-side':    (7, 0, 200, 4, -240, 110, 12, 'the same from the side: a jet out of the niche into the room'),
    'ledge-edge':     (8, 0, 150, 2, -260, 100, 8, 'a barrel at the edge of a 96 drop, from the side: fire and smoke over the edge too'),
    'yard-wall':      (9, 0, 150, 4, -260, 120, 8, 'a rocket on a wall under open sky, from the side: up the wall and on above it'),
    'yard-floor':     (9, 0, 40, 2, 0, -260, 90, 'a barrel under open sky: a free mushroom'),
}

def write(path):
    lumps = []
    for i, rects in enumerate(rooms()):
        lumps.append(('E1M%d' % (i + 1), b''))
        lumps += arena.build(rects, MONSTERS, row_y=96) if MONSTERS else arena.build(rects, 'zombie', start=(0, -200, 90))
    body, directory, offset = b'', b'', 12
    for name, data in lumps:
        directory += struct.pack('<2i', offset, len(data)) + arena.name8(name)
        body += data; offset += len(data)
    with open(path, 'wb') as out:
        out.write(b'PWAD' + struct.pack('<2i', len(lumps), offset) + body + directory)

MONSTERS = None   # --monsters KIND: a row of four of them at y 96 (ragdoll_arena.py's kinds), the player at 0,-32

def main():
    global MONSTERS
    args = sys.argv[1:]
    def option(name, default):
        if name in args:
            at = args.index(name); value = args[at + 1]; del args[at:at + 2]
            return value
        return default
    MONSTERS = option('--monsters', None)
    mode, kind, every, frames = option('--mode', '2'), option('--kind', None), option('--every', '2'), option('--frames', '12')
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.abspath(option('--out', os.path.join(here, '..', 'output', 'explosion-rooms')))
    if '--list' in args:
        for name, case in CASES.items():
            print('%-15s E1M%d  %s' % (name, case[0], case[7]))
        return
    os.makedirs(out, exist_ok=True)
    game, iwad, mine = arena.machine()
    game, iwad = os.path.abspath(game), os.path.abspath(iwad.replace('doom2.wad', 'doom.wad'))
    for needed in (game, iwad):
        if not os.path.exists(needed):
            raise SystemExit('missing: ' + needed)
    wad = os.path.join(out, 'explosion-rooms.wad')
    write(wad)
    for name, yours in (('graphics.cfg', 'graphics.cfg'), ('default.cfg', 'doom.cfg')):   # fresh copies, never written back
        shutil.copy(os.path.join(mine, yours), os.path.join(out, name))
    graphics = os.path.join(out, 'graphics.cfg')
    text = open(graphics, newline='').read()
    text = re.sub(r'(?m)^explosions \d+', 'explosions ' + mode, text)
    text = re.sub(r'(?m)^show_fps \d+', 'show_fps 0', text)
    open(graphics, 'w', newline='').write(text)
    for name in (args or list(CASES)):
        level, x, y, case_kind, vx, vy, angle, _ = CASES[name]
        sheet = os.path.join(out, '%s-mode%s.png' % (name, mode))
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', DERP_EXPLODE='%d,%d,40,%s' % (x, y, kind or case_kind))
        command = [game, '-iwad', iwad, '-file', wad, '-warp', '1', str(level)] + ([] if MONSTERS else ['-nomonsters']) + [
                   '-config', os.path.join(out, 'default.cfg'), '-graphics', graphics,
                   '-rendercheck', sheet, '-rendertic', '40', '-renderevery', every, '-renderframes', frames, '-rendersheet', '4',
                   '-rendercrop', os.environ.get('CROP', '0.15,0.05,0.85,0.8'), '-renderscale', os.environ.get('SCALE', '0.45'), '-renderview', str(vx), str(vy), str(angle)]
        done = subprocess.run(command, env=env, capture_output=True, text=True, timeout=300)
        print('%s: %s' % (name, 'ok' if os.path.exists(sheet) and done.returncode == 0 else 'FAILED (exit %d) %s' % (done.returncode, done.stderr[-400:])))

if __name__ == '__main__':
    main()
