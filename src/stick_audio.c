/* Stick audio: OSS on /dev/dsp, NON-blocking, plus the /dev/mixer volume.
 * gp_audio.ko's blocking write waits for space with msleep(1), which at HZ=100 sleeps 10 to 20 ms:
 * far too coarse to pace frames. Non-blocking mode can only be enabled with SNDCTL_DSP_NONBLOCK (the
 * driver ignores O_NONBLOCK on open). The TV retrace holds the pacing; the runner adjusts the audio
 * rate from the queue (SNDCTL_DSP_GETODELAY). */
#include <errno.h>
#include <fcntl.h>
#include <linux/soundcard.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "plat.h"
#include "stick_internal.h"

#define AUDIO_OUTPUT_HDMI 0xc0045007 /* gp_audio: 1 = HDMI I2S, 0 = analog output (default) */
#define BYTES_PER_AUDIO_FRAME 4      /* 16-bit stereo */
#define CHANNELS 2
#define FRAGMENT_COUNT 32
#define FRAGMENT_SIZE_LOG2 10 /* 32 fragments of 1 KB (~170 ms at 48 kHz) */
#define MIXER_VOLUME 90       /* 0 to 100 */
#define MIXER_RIGHT_SHIFT 8   /* OSS: left | right << 8 */
#define MIXER_LEFT_MASK 0xff

static int dsp = -1, dsp_rate;

/* gp_audio.ko sends to the analog output (headphone/AV) by default; ioctl 0xc0045007 with 1 switches
 * to HDMI I2S (DMA "ocp"). Without it the audio goes somewhere the TV does not hear.
 * It must come BEFORE SETFRAGMENT: that rebuilds the fragments. */
static void select_output(void)
{
    const char *output = getenv("STICK_AUDIO_OUTPUT");
    int hdmi = !(output && *output == '0');
    if (ioctl(dsp, AUDIO_OUTPUT_HDMI, &hdmi)) {
        stick_step("audio: output selection (0xc0045007) failed", 0, 0);
    }
    stick_step("audio: output hdmi %d", hdmi, 0);
}

/* without the stock 10 in 1 nobody sets the mixer volume */
static void set_mixer_volume(void)
{
    int mixer = open("/dev/mixer", O_WRONLY);
    if (mixer < 0) {
        stick_step("audio: could not open /dev/mixer", 0, 0);

        return;
    }
    int volume = MIXER_VOLUME | MIXER_VOLUME << MIXER_RIGHT_SHIFT;
    if (ioctl(mixer, SOUND_MIXER_WRITE_VOLUME, &volume)) {
        stick_step("audio: mixer volume failed", 0, 0);
    } else {
        stick_step("audio: mixer volume %d", volume & MIXER_LEFT_MASK, 0);
    }
    close(mixer);
}

int plat_audio_init(int rate)
{
    dsp = open("/dev/dsp", O_WRONLY);
    if (dsp < 0) {
        stick_step("audio: could not open /dev/dsp (busy, is the factory emulator running?)", 0, 0);

        return -1;
    }
    select_output();
    int format = AFMT_S16_LE;
    int channels = CHANNELS;
    int accepted_rate = rate;
    int fragments = (FRAGMENT_COUNT << 16) | FRAGMENT_SIZE_LOG2;
    ioctl(dsp, SNDCTL_DSP_SETFRAGMENT, &fragments);
    ioctl(dsp, SNDCTL_DSP_SETFMT, &format);
    ioctl(dsp, SNDCTL_DSP_CHANNELS, &channels);
    ioctl(dsp, SNDCTL_DSP_SPEED, &accepted_rate);
    stick_step("audio: requested rate %d, accepted %d", rate, accepted_rate);
    dsp_rate = accepted_rate;
    audio_buf_info space;
    if (!ioctl(dsp, SNDCTL_DSP_GETOSPACE, &space)) {
        stick_step("audio: %d fragments of %d bytes", space.fragstotal, space.fragsize);
    }
    if (ioctl(dsp, SNDCTL_DSP_NONBLOCK, 0)) {
        stick_step("audio: SNDCTL_DSP_NONBLOCK failed", 0, 0);
    }
    set_mixer_volume();

    return 0;
}

int plat_audio(const int16_t *samples, int frames)
{
    if (dsp < 0) {
        return 0;
    }
    int written = write(dsp, samples, frames * BYTES_PER_AUDIO_FRAME);
    if (written >= 0) {
        return written / BYTES_PER_AUDIO_FRAME;
    }
    if (errno != EAGAIN) {
        close(dsp);
        dsp = -1;
    }

    return 0;
}

int plat_audio_rate(void)
{
    return dsp >= 0 ? dsp_rate : 0;
}

void plat_audio_close(void)
{
    if (dsp >= 0) {
        close(dsp);
    }
    dsp = -1;
}

int plat_audio_queue(void)
{
    int queued_bytes;
    if (dsp < 0 || ioctl(dsp, SNDCTL_DSP_GETODELAY, &queued_bytes)) {
        return -1;
    }

    return queued_bytes / BYTES_PER_AUDIO_FRAME;
}
