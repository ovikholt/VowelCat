// Copyright 2014 Formant Industries. See the Copying file at the top-level
// directory of this project.

#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdlib.h>

#include <QObject>

extern "C" {
#include "audio.h"
#include "formant.h"
}

#include "params.h"
#include "plotter.h"
#include "timespec.h"

Plotter::Plotter(audio_t *a) :
    run(false),
    audio(a)
{
    sound_init(&sound);

    formant_opts_init(&opts);
    opts.pre_emph_factor = 1;

    if (!formant_opts_process(&opts))
        abort();
}

Plotter::~Plotter() {
    sound_destroy(&sound);
}

#define ABS(x) ((x) > 0 ? (x) : -(x))

bool Plotter::noise() {
    enum { NOISE_STRIDE = SAMPLES_PER_CHUNK / NOISE_SAMPLES };
    uintmax_t avg = 0;

    for (size_t i = 0; i < sound.n_samples; i += NOISE_STRIDE)
        avg += ABS(sound.samples[i]);

    return avg < NOISE_THRESHOLD * NOISE_SAMPLES;
}

void Plotter::formant(uintmax_t &f1, uintmax_t &f2) {
    if (!sound_calc_formants(&sound, &opts))
        abort();

    f1 = 0;

    for (size_t i = 0; i < sound.n_samples; i += 1)
        f1 += sound_get_f1(&sound, i);

    f2 = 0;

    for (size_t i = 0; i < sound.n_samples; i += 1)
        f2 += sound_get_f2(&sound, i);

    f1 /= sound.n_samples;
    f2 /= sound.n_samples;
}

void Plotter::listen_run() {
    uintmax_t f1, f2;
    timespec_t before, after;
    uintmax_t dur = 0, count = 0;

    if (!audio_record(audio))
        abort();

    while(run) {
        timespec_init(&before);
        sound_reset(&sound, SAMPLE_RATE, CHANNELS);
        sound_resize(&sound, SAMPLES_PER_CHUNK);

        //***********************
        if(!audio_listen_read(audio, sound.samples))
            break;
        //***********************

        if(noise())
           continue;

        formant(f1, f2);
        timespec_init(&after);

        dur = (timespec_diff(&before, &after) + count * dur) / (count + 1);
        count += 1;

        emit newFormant(f1, f2, dur);
    }
}

void Plotter::record_run() {
    uintmax_t f1, f2;
    timespec_t before, after;
    uintmax_t dur = 0, count = 0;

    if (!audio_record(audio))
        abort();

    while(run) { //Pa_IsStreamActive does not seem to work quick enough
        timespec_init(&before);
        sound_reset(&sound, SAMPLE_RATE, CHANNELS);
        sound_resize(&sound, SAMPLES_PER_CHUNK);

        //***********************
        if(!audio_record_read(audio, sound.samples))
            break;
        //***********************

        if(noise())
            continue;

        formant(f1, f2);

        timespec_init(&after);

        dur = (timespec_diff(&before, &after) + count * dur) / (count + 1);
        count += 1;

        emit newFormant(f1, f2, dur);
    }
}

void Plotter::play_run() {
    uintmax_t f1, f2;
    timespec_t before, after;
    uintmax_t dur = 0, count = 0;

    if (!audio_play(audio))
        abort();

    while(run) { //Pa_IsStreamActive does not seem to work quick enough
        timespec_init(&before);
        sound_reset(&sound, SAMPLE_RATE, CHANNELS);
        sound_resize(&sound, SAMPLES_PER_CHUNK);

        //***********************
        if(!audio_play_read(audio, sound.samples))
            break;
        //***********************

        if(noise())
            continue;

        formant(f1, f2);

        timespec_init(&after);

        dur = (timespec_diff(&before, &after) + count * dur) / (count + 1);
        count += 1;

        emit newFormant(f1, f2, dur);
    }

    if(run)
        emit pauseSig();
}

bool Plotter::calcFormant(size_t offset, uintmax_t &f1, uintmax_t  &f2) {
    sound_reset(&sound, SAMPLE_RATE, CHANNELS);
    sound_resize(&sound, SAMPLES_PER_CHUNK);

    memcpy(&sound.samples[0], &audio->prbuf[offset], audio->samples_per_chunk * sizeof(audio_sample_t));

    if (noise())
        return false;

    formant(f1, f2);

    return true;
}

static void listen_helper(Plotter *p) {
    p->listen_run();
}

static void record_helper(Plotter *p) {
    p->record_run();
}

static void play_helper(Plotter *p) {
    p->play_run();
}

void Plotter::listen() {
    run = true;

    pthread_create(&tid, NULL, [] (void *data) -> void * {
        listen_helper((Plotter *) data);
        pthread_exit(NULL);
    }, this);
}

void Plotter::record() {
    run = true;

    pthread_create(&tid, NULL, [] (void *data) -> void * {
        record_helper((Plotter *) data);
        pthread_exit(NULL);
    }, this);
}

void Plotter::play() {
    run = true;

    pthread_create(&tid, NULL, [] (void *data) -> void * {
        play_helper((Plotter *) data);
        pthread_exit(NULL);
    }, this);
}

void Plotter::stop() {
    // Prevent the plotter thread from doing any more work.
    run = false;

    // Wakeup the plotter thread in case it's sleeping, then wait for it to
    // finish.
    audio_stop(audio);
    pthread_join(tid, NULL);
}
