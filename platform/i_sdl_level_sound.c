/* Sound through the whole level on authored maps, for the SDL mixer: the
 * far sounds DOOM's channels can't take, sleepers stirring, the far rooms
 * ringing and the listener's own shots spilling into them. What the sound
 * does is in audio/level_sound.h, room_records.h and room_reverbs.h; this
 * is where it meets the mixer's channels (i_sdl_mixer.h). */
#include <stdio.h>
#include "doomdef.h"
#include "doomstat.h"
#include "i_sound.h"
#include "r_state.h"
#include "m_argv.h"
#include "audio/level_sound.h"
#include "audio/occlusion.h"
#include "audio/room_grid.h"
#include "audio/room_records.h"
#include "audio/room_reverbs.h"
#include "audio/room_tails.h"
#include "audio/sound_options.h"
#include "i_sdl_mixer.h"

/* After the listener's own sound (a shot), how long the rooms next door
   take it in, and the most of it the widest opening lets through. */
#define OWN_SPILL_TICS 18
#define OWN_SPILL 0.5f
#define OWN_SPILL_WIDE 256.0f

/* How many far sounds have played (-levelsoundlog). */
static int far_played;
/* The listener's room from the baked grid (F4), not live rays. */
int room_grid = 1;

/* Authored maps, once a tic: whether the level carries sound, its rooms
   measured, and the far sounds now due. */
static void play_far(const far_sound_t *far);
static void far_sounds_now(void);
void Mixer_LevelTic(void)
{
    Uint64 start = SDL_GetTicksNS();
    far_sounds_now();
    spent_game_ns += SDL_GetTicksNS() - start;
}
static void far_sounds_now(void)
{
    static int was;
    static unsigned level = ~0u;
    int on = LevelSound_Follow();
    /* A new level's rooms are other rooms, though their keys may be the same. */
    if (on != was || (on && level != r_levelserial))
    {
        was = on; level = r_levelserial;
        SDL_LockAudioStream(stream);
        RoomReverbs_Clear();
        RoomTails_Clear();
        RoomTails_Forget();
        for (int c = 0; c < MIX_CHANNELS; ++c) { channels[c].slot = -1; channels[c].key = -1; }
        SDL_UnlockAudioStream(stream);
        if (!on) { RoomGrid_Stop(); RoomTails_Stop(); }
    }
    /* -levelsoundlog: once a second, what carries through the level. */
    static int log = -1;
    if (log < 0) log = M_CheckParm("-levelsoundlog") > 0;
    if (!on)
    {
        if (log && gametic % TICRATE == 0)
        {
            fprintf(stderr, "levelsound tic %d: off; mixer %.2f ms/s in all\n", gametic, spent_audio_ns / 1e6);
            spent_audio_ns = 0;
        }
        return;
    }
    RoomRecords_Follow();
    RoomTails_Follow();
    if (room_grid) RoomGrid_Follow();
    far_sound_t far;
    while (LevelSound_Due(&far)) play_far(&far);
    const mobj_t *listener = players[consoleplayer].mo;
    if (listener && LevelSound_Stir(listener->x / (float)FRACUNIT, listener->y / (float)FRACUNIT, &far)) play_far(&far);
    if (log && gametic % TICRATE == 0)
    {
        int voices = 0, far_voices = 0, ringing = 0;
        SDL_LockAudioStream(stream);
        for (int c = 0; c < MIX_CHANNELS; ++c)
            if (channels[c].handle) { ++voices; far_voices += channels[c].far; ringing += channels[c].slot >= 0; }
        int keys[ROOM_SLOTS];
        for (int i = 0; i < ROOM_SLOTS; ++i) keys[i] = RoomReverbs_Key(i);
        SDL_UnlockAudioStream(stream);
        fprintf(stderr, "levelsound tic %d: voices %d far %d (%d so far) in far rooms %d, rooms %d %d %d;"
                " game %.0f us/tic, far rooms %.2f ms/s of mixing, mixer %.2f ms/s in all\n",
                gametic, voices, far_voices, far_played, ringing, keys[0], keys[1], keys[2],
                spent_game_ns / 1000.0 / TICRATE, spent_mix_ns / 1e6, spent_audio_ns / 1e6);
        fprintf(stderr, "levelsound tic %d: listener's room %d from the grid, %d measured, %.1f us each\n", gametic,
                room_baked, room_live, room_baked + room_live ? room_ns / 1000.0 / (room_baked + room_live) : 0.0);
        spent_game_ns = spent_mix_ns = spent_audio_ns = 0;
        room_ns = 0; room_baked = room_live = 0;
    }
}
/* Every other tic: each sound made in another room rings in that room's
   reverb, heard the way the room reaches the listener. */
void Mixer_LevelRooms(void)
{
    if (!LevelSound_On()) return;
    const mobj_t *listener = players[consoleplayer].mo;
    int here = listener ? RoomRecords_KeyAt(listener->x / (float)FRACUNIT, listener->y / (float)FRACUNIT) : -1;
    int keys[ROOM_SLOTS];
    SDL_LockAudioStream(stream);
    RoomReverbs_Expire();
    for (int c = 0; c < MIX_CHANNELS; ++c)
    {
        mix_channel_t *channel = &channels[c];
        if (!channel->handle) continue;
        int key = channel->key, slot = -1;
        if ((channel->placed || channel->anchored) && key >= 0 && key != here && channel->beyond_target < 0.5f)
            slot = RoomReverbs_Claim(key, RoomRecords_Get(key));
        if (slot != channel->slot)
        {
            channel->slot = slot;
            gains(channel, channel->volume, channel->separation, channel->pitch);
        }
    }
    /* A shot of the listener's rings on in the rooms next door, through the
       widest openings; the bigger space beyond (beyond.h) already has it. */
    for (int i = 0; i < ROOM_SLOTS; ++i) RoomReverbs_SetOwn(i, 0);
    if (here >= 0 && gametic >= own_tic && gametic - own_tic < OWN_SPILL_TICS)
    {
        int next[2];
        float widths[2];
        int count = RoomRecords_Neighbours(here, next, widths, 2);
        for (int k = 0; k < count; ++k)
        {
            const room_record_t *record = RoomRecords_Get(next[k]);
            if (!record || Beyond_Share(&beyond, record->x, record->y) > 0) continue;
            int slot = RoomReverbs_Claim(next[k], record);
            if (slot >= 0) RoomReverbs_SetOwn(slot, OWN_SPILL * SDL_min(widths[k] / OWN_SPILL_WIDE, 1.0f));
        }
    }
    for (int i = 0; i < ROOM_SLOTS; ++i) keys[i] = RoomReverbs_Key(i);
    SDL_UnlockAudioStream(stream);
    for (int i = 0; i < ROOM_SLOTS; ++i)
    {
        const room_record_t *record = keys[i] >= 0 ? RoomRecords_Get(keys[i]) : NULL;
        if (!record) continue;
        float gain, cutoff, fx, fy;
        RoomReverbs_Path(record, &gain, &cutoff, &fx, &fy);
        int separation = pan_at(fx, fy);
        SDL_LockAudioStream(stream);
        if (RoomReverbs_Key(i) == keys[i]) RoomReverbs_SetPath(i, gain, cutoff, separation);
        SDL_UnlockAudioStream(stream);
    }
}

void I_SetLevelSound(int authored, int far_rooms, int grid)
{
    if (M_CheckParm("-levelsoundlog")) fprintf(stderr, "levelsound: authored %d, far rooms %d, grid %d\n", authored, far_rooms, grid);
    LevelSound_Set(authored);
    /* Live rays from now on: the workers stop and the grid is let go. */
    if (room_grid && !grid) RoomGrid_Stop();
    room_grid = grid != 0;
    if (stream) SDL_LockAudioStream(stream);
    RoomReverbs_SetCount(far_rooms);
    if (stream) SDL_UnlockAudioStream(stream);
}
void I_SetRoomTails(int traced) { RoomTails_Set(traced); }
int I_LevelSound(void) { return LevelSound_On() && gamemap != 8; }
/* A sound too far for one of DOOM's channels: queued, to be heard as late
   as sound takes to come that far, unless it would be too faint. */
void I_FarSound(int id, fixed_t x, fixed_t y, fixed_t z, int volume)
{
    if (!stream || !by_path() || id < 1 || id >= NUMSFX || !samples[id].data || snd_SfxVolume <= 0) return;
    float level = SDL_clamp(volume, 0, snd_SfxVolume) / (float)snd_SfxVolume, reach = reach_of(id);
    occlusion_path_t path;
    Occlusion_Path(x, y, z, &path);
    if (LevelSound_Gain(path.length, path.loss, reach) * level <= 0) return;
    LevelSound_Queue(id, x / (float)FRACUNIT, y / (float)FRACUNIT, z / (float)FRACUNIT, level, reach, path.length);
}
static void play_far(const far_sound_t *far)
{
    const sample_t *sample = &samples[far->id];
    if (!sample->data) return;
    through_far = 1;
    play_through(&(made_sample_t){ sample->data, sample->length, sample->rate }, far->x, far->y, far->z, far->level, far->reach,
                 OCCLUSION_OPEN_HZ, 0);
    through_far = 0;
    far_played += last_through != 0;
}
