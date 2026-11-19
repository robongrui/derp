/* Recorded music streamed from an MP3 or Ogg Vorbis file in memory: decoded
   a little at a time in the mixer, and read at the mixer's rate through a
   4-point cubic (the packs mix 22.05, 44.1 and 48 kHz songs). dr_mp3
   (public domain, David Reid, after minimp3) and stb_vorbis (public domain,
   Sean Barrett; built in vorbis_decoder.c) decode. */
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"
#include <stdlib.h>
#include <string.h>
#include "recorded_music.h"

#define CHUNK 2048	/* frames decoded at a time */

static drmp3	mp3;
static stb_vorbis*	vorbis;
static void*	data;
static int	loaded, playing, looping, ended, channels;
static float	track_gain = 1;
static double	position, step;	/* in buffer frames; 1 is the first not yet read past */
static int	held;		/* frames in the buffer */
static float	buffer[(CHUNK + 4) * 2], decoded[CHUNK * 8];

static int is_ogg (const unsigned char* p, size_t size) { return size >= 4 && !memcmp (p, "OggS", 4); }

int Recorded_Is (const void* lump, size_t size)
{
    const unsigned char* p = lump;
    if (size < 4)
	return 0;
    if (is_ogg (p, size) || !memcmp (p, "ID3", 3))
	return 1;
    /* An MP3 frame sync with layer III. */
    return p[0] == 0xff && (p[1] & 0xe0) == 0xe0 && (p[1] & 0x06) == 0x02;
}

void Recorded_Unload (void)
{
    if (vorbis)
	stb_vorbis_close (vorbis);
    else if (loaded)
	drmp3_uninit (&mp3);
    vorbis = NULL;
    free (data);
    data = NULL;
    loaded = playing = 0;
}

int Recorded_Load (void* bytes, size_t size, int rate, float gain)
{
    int source_rate = 0;
    Recorded_Unload ();
    data = bytes;
    if (is_ogg (data, size))
    {
	int error = 0;
	vorbis = size < 0x7fffffff ? stb_vorbis_open_memory (data, (int) size, &error, NULL) : NULL;
	if (vorbis)
	{
	    stb_vorbis_info info = stb_vorbis_get_info (vorbis);
	    source_rate = (int) info.sample_rate;
	    channels = info.channels;
	}
    }
    else if (drmp3_init_memory (&mp3, data, size, NULL))
    {
	loaded = 1;
	source_rate = (int) mp3.sampleRate;
	channels = (int) mp3.channels;
    }
    loaded = vorbis || loaded;
    if (!loaded || source_rate <= 0 || channels < 1 || channels > 8)
    {
	Recorded_Unload ();
	return 0;
    }
    step = source_rate / (double) rate;
    track_gain = gain;
    return 1;
}

static void rewind_song (void)
{
    if (vorbis)
	stb_vorbis_seek_start (vorbis);
    else
	drmp3_seek_to_pcm_frame (&mp3, 0);
}

static int decode (int frames)
{
    if (vorbis)
	return stb_vorbis_get_samples_float_interleaved (vorbis, channels, decoded, frames * channels);
    return (int) drmp3_read_pcm_frames_f32 (&mp3, (drmp3_uint64) frames, decoded);
}

void Recorded_Start (int loop)
{
    if (!loaded)
	return;
    rewind_song ();
    buffer[0] = buffer[1] = 0;	/* silence before the first frame */
    held = 1;
    position = 1;
    looping = loop;
    ended = 0;
    playing = 1;
}

void Recorded_Stop (void) { playing = 0; }
int Recorded_Playing (void) { return playing; }

/* Decodes onto the buffer's end; past the song's end, loops or pads silence. */
static void refill (void)
{
    int first = (int) position - 1;
    memmove (buffer, buffer + first * 2, (size_t) (held - first) * 2 * sizeof (float));
    held -= first;
    position -= first;
    int want = CHUNK + 4 - held;
    if (want > CHUNK)
	want = CHUNK;
    int got = ended ? 0 : decode (want);
    if (!got && !ended && looping)
    {
	rewind_song ();
	got = decode (want);
    }
    if (got <= 0)
    {
	/* Once it has played out, the reader stops in the silence after it. */
	if (ended && position > held - 2)
	    playing = 0;
	ended = 1;
	memset (buffer + held * 2, 0, (size_t) want * 2 * sizeof (float));
	held += want;
	return;
    }
    float* out = buffer + held * 2;
    for (int i = 0; i < got; ++i)
    {
	out[i * 2] = decoded[i * channels];
	out[i * 2 + 1] = decoded[i * channels + (channels > 1)];
    }
    held += got;
}

static float cubic (const float* x, float t)
{
    /* x[-2], x[0], x[2], x[4]: one channel of four stereo frames. */
    float a = x[-2], b = x[0], c = x[2], d = x[4];
    return b + 0.5f * t * (c - a + t * (2 * a - 5 * b + 4 * c - d + t * (3 * (b - c) + d - a)));
}

void Recorded_Mix (float* stereo, int frames, float gain)
{
    gain *= track_gain;
    for (int i = 0; i < frames && playing; ++i)
    {
	while ((int) position + 2 >= held && playing)
	    refill ();
	if (!playing)
	    break;
	int at = (int) position;
	float t = (float) (position - at);
	const float* x = buffer + at * 2;
	stereo[i * 2] += gain * cubic (x, t);
	stereo[i * 2 + 1] += gain * cubic (x + 1, t);
	position += step;
    }
}
