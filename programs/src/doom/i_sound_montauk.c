/*
    * i_sound_montauk.c
    * DOOM sound effects and music modules for MontaukOS
    * Implements software mixing of 16 SFX channels into the HDA audio device.
    * Copyright (c) 2026 Daniel Hammer
    */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "deh_str.h"
#include "i_sound.h"
#include "i_system.h"
#include "i_swap.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"
#include "doomtype.h"

/* ========================================================================
   Raw syscall interface for audio
   ======================================================================== */

static inline long _snd_syscall1(long nr, long a1) {
    long ret;
    __asm__ volatile(
        "mov %[a1], %%rdi\n\t"
        "syscall"
        : "=a"(ret)
        : "a"(nr), [a1] "r"(a1)
        : "rcx", "r11", "rdi", "rsi", "rdx", "r8", "r9", "r10", "memory");
    return ret;
}

static inline long _snd_syscall3(long nr, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile(
        "mov %[a1], %%rdi\n\t"
        "mov %[a2], %%rsi\n\t"
        "mov %[a3], %%rdx\n\t"
        "syscall"
        : "=a"(ret)
        : "a"(nr), [a1] "r"(a1), [a2] "r"(a2), [a3] "r"(a3)
        : "rcx", "r11", "rdi", "rsi", "rdx", "r8", "r9", "r10", "memory");
    return ret;
}

#define SYS_AUDIOOPEN   80
#define SYS_AUDIOCLOSE  81
#define SYS_AUDIOWRITE  82

/* ========================================================================
   Audio mixing engine
   ======================================================================== */

/* Required by i_sound.c config binding */
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;

#define NUM_CHANNELS    16
#define MIX_RATE        44100
#define MIX_CHANNELS    2
#define MIX_BITS        16

/* Samples per mix frame (~35 fps, slightly oversized for timing jitter) */
#define MIX_SAMPLES     1260
#define MIX_BUF_BYTES   (MIX_SAMPLES * MIX_CHANNELS * (MIX_BITS / 8))

/* Channel state */
struct snd_channel {
    const uint8_t  *data;       /* 8-bit unsigned PCM samples */
    uint32_t        length;     /* number of samples */
    uint32_t        pos;        /* playback position (16.16 fixed-point) */
    uint32_t        step;       /* step per output sample (16.16 fixed-point) */
    int             vol_left;   /* left volume (0-127) */
    int             vol_right;  /* right volume (0-127) */
    sfxinfo_t      *sfxinfo;    /* NULL = inactive */
};

static struct snd_channel channels[NUM_CHANNELS];
static int audio_handle = -1;
static boolean sound_initialized = false;
static boolean use_sfx_prefix;

/* Static mix buffer (stereo interleaved int16_t) */
static int16_t mix_buf[MIX_SAMPLES * MIX_CHANNELS];

/* ========================================================================
   Sound effect loading (DMX format from WAD lumps)
   ======================================================================== */

static void GetSfxLumpName(sfxinfo_t *sfx, char *buf, size_t buf_len) {
    if (sfx->link != NULL)
        sfx = sfx->link;

    if (use_sfx_prefix)
        M_snprintf(buf, buf_len, "ds%s", DEH_String(sfx->name));
    else
        M_StringCopy(buf, DEH_String(sfx->name), buf_len);
}

/* Parse a DMX sound lump. Returns the raw 8-bit PCM data and fills in
   samplerate and length. Returns NULL on failure. */
static const uint8_t *ParseDmxSound(int lumpnum, int *out_rate, uint32_t *out_len) {
    unsigned int lumplen = W_LumpLength(lumpnum);
    const uint8_t *data = W_CacheLumpNum(lumpnum, PU_STATIC);

    if (lumplen < 8 || data[0] != 0x03 || data[1] != 0x00)
        return NULL;

    int samplerate = (data[3] << 8) | data[2];
    uint32_t length = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];

    if (length > lumplen - 8 || length <= 48)
        return NULL;

    /* DMX skips first 16 and last 16 bytes of sample data */
    *out_rate = samplerate;
    *out_len = length - 32;
    return data + 16;
}

/* ========================================================================
   Volume/separation helpers
   ======================================================================== */

static void SetChannelVolSep(int ch, int vol, int sep) {
    /* vol: 0-127, sep: 0-254 (0=full left, 127=center, 254=full right) */
    channels[ch].vol_left  = ((254 - sep) * vol) / 127;
    channels[ch].vol_right = (sep * vol) / 127;
}

/* ========================================================================
   Software mixer - mix all active channels into mix_buf
   ======================================================================== */

static void MixChannels(int num_samples) {
    /* Clear mix buffer */
    memset(mix_buf, 0, (size_t)(num_samples * MIX_CHANNELS) * sizeof(int16_t));

    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        struct snd_channel *c = &channels[ch];
        if (c->sfxinfo == NULL)
            continue;

        int16_t *out = mix_buf;

        for (int i = 0; i < num_samples; i++) {
            uint32_t ipos = c->pos >> 16;

            if (ipos >= c->length) {
                /* Sound finished */
                c->sfxinfo = NULL;
                break;
            }

            /* Convert 8-bit unsigned (0-255, 128=silence) to signed (-128..127)
               then scale to 16-bit range */
            int sample = ((int)c->data[ipos] - 128) << 8;

            /* Apply volume and accumulate (additive mixing) */
            int left  = out[i * 2 + 0] + ((sample * c->vol_left)  >> 7);
            int right = out[i * 2 + 1] + ((sample * c->vol_right) >> 7);

            /* Clamp to int16_t range */
            if (left > 32767) left = 32767;
            else if (left < -32768) left = -32768;
            if (right > 32767) right = 32767;
            else if (right < -32768) right = -32768;

            out[i * 2 + 0] = (int16_t)left;
            out[i * 2 + 1] = (int16_t)right;

            c->pos += c->step;
        }
    }
}

/* ========================================================================
   sound_module_t implementation
   ======================================================================== */

static boolean I_Montauk_InitSound(boolean _use_sfx_prefix) {
    use_sfx_prefix = _use_sfx_prefix;

    for (int i = 0; i < NUM_CHANNELS; i++) {
        memset(&channels[i], 0, sizeof(channels[i]));
    }

    audio_handle = (int)_snd_syscall3(SYS_AUDIOOPEN,
        (long)MIX_RATE, (long)MIX_CHANNELS, (long)MIX_BITS);

    if (audio_handle < 0) {
        return false;
    }

    sound_initialized = true;
    return true;
}

static void I_Montauk_ShutdownSound(void) {
    if (!sound_initialized)
        return;

    for (int i = 0; i < NUM_CHANNELS; i++)
        channels[i].sfxinfo = NULL;

    if (audio_handle >= 0) {
        _snd_syscall1(SYS_AUDIOCLOSE, (long)audio_handle);
        audio_handle = -1;
    }

    sound_initialized = false;
}

static int I_Montauk_GetSfxLumpNum(sfxinfo_t *sfx) {
    char namebuf[9];
    GetSfxLumpName(sfx, namebuf, sizeof(namebuf));
    return W_GetNumForName(namebuf);
}

static void I_Montauk_UpdateSound(void) {
    if (!sound_initialized || audio_handle < 0)
        return;

    /* Mix one frame of audio and submit to the device */
    MixChannels(MIX_SAMPLES);

    const uint8_t *ptr = (const uint8_t *)mix_buf;
    int remaining = MIX_BUF_BYTES;

    /* Write as much as the device accepts (non-blocking) */
    while (remaining > 0) {
        int written = (int)_snd_syscall3(SYS_AUDIOWRITE,
            (long)audio_handle, (long)ptr, (long)remaining);
        if (written <= 0)
            break;
        ptr += written;
        remaining -= written;
    }
}

static void I_Montauk_UpdateSoundParams(int channel, int vol, int sep) {
    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS)
        return;
    if (channels[channel].sfxinfo == NULL)
        return;
    SetChannelVolSep(channel, vol, sep);
}

static int I_Montauk_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep) {
    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS)
        return -1;

    /* Stop any sound already on this channel */
    channels[channel].sfxinfo = NULL;

    /* Load and parse the sound lump */
    if (sfxinfo->lumpnum == -1)
        return -1;

    int samplerate;
    uint32_t length;
    const uint8_t *data = ParseDmxSound(sfxinfo->lumpnum, &samplerate, &length);
    if (data == NULL)
        return -1;

    /* Set up the channel */
    channels[channel].data    = data;
    channels[channel].length  = length;
    channels[channel].pos     = 0;
    /* Resampling step: source_rate / MIX_RATE in 16.16 fixed-point */
    channels[channel].step    = ((uint32_t)samplerate << 16) / MIX_RATE;

    SetChannelVolSep(channel, vol, sep);
    channels[channel].sfxinfo = sfxinfo;

    return channel;
}

static void I_Montauk_StopSound(int channel) {
    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS)
        return;
    channels[channel].sfxinfo = NULL;
}

static boolean I_Montauk_SoundIsPlaying(int channel) {
    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS)
        return false;
    return channels[channel].sfxinfo != NULL;
}

static void I_Montauk_PrecacheSounds(sfxinfo_t *sounds, int num_sounds) {
    /* No-op: we load on demand */
    (void)sounds;
    (void)num_sounds;
}

static snddevice_t sound_montauk_devices[] = {
    SNDDEVICE_SB,
    SNDDEVICE_PAS,
    SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS,
    SNDDEVICE_AWE32,
};

sound_module_t DG_sound_module = {
    sound_montauk_devices,
    arrlen(sound_montauk_devices),
    I_Montauk_InitSound,
    I_Montauk_ShutdownSound,
    I_Montauk_GetSfxLumpNum,
    I_Montauk_UpdateSound,
    I_Montauk_UpdateSoundParams,
    I_Montauk_StartSound,
    I_Montauk_StopSound,
    I_Montauk_SoundIsPlaying,
    I_Montauk_PrecacheSounds,
};

/* ========================================================================
   music_module_t implementation (stub - no MIDI synthesis)
   ======================================================================== */

static boolean I_Montauk_InitMusic(void)          { return true; }
static void    I_Montauk_ShutdownMusic(void)       {}
static void    I_Montauk_SetMusicVolume(int vol)   { (void)vol; }
static void    I_Montauk_PauseMusic(void)          {}
static void    I_Montauk_ResumeMusic(void)         {}
static void   *I_Montauk_RegisterSong(void *data, int len) { (void)data; (void)len; return (void *)1; }
static void    I_Montauk_UnRegisterSong(void *h)   { (void)h; }
static void    I_Montauk_PlaySong(void *h, boolean loop) { (void)h; (void)loop; }
static void    I_Montauk_StopSong(void)            {}
static boolean I_Montauk_MusicIsPlaying(void)      { return false; }
static void    I_Montauk_PollMusic(void)           {}

static snddevice_t music_montauk_devices[] = {
    SNDDEVICE_SB,
    SNDDEVICE_ADLIB,
    SNDDEVICE_GUS,
};

music_module_t DG_music_module = {
    music_montauk_devices,
    arrlen(music_montauk_devices),
    I_Montauk_InitMusic,
    I_Montauk_ShutdownMusic,
    I_Montauk_SetMusicVolume,
    I_Montauk_PauseMusic,
    I_Montauk_ResumeMusic,
    I_Montauk_RegisterSong,
    I_Montauk_UnRegisterSong,
    I_Montauk_PlaySong,
    I_Montauk_StopSong,
    I_Montauk_MusicIsPlaying,
    I_Montauk_PollMusic,
};
