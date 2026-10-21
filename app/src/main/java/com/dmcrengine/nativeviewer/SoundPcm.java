package com.dmcrengine.nativeviewer;

/** Passive 16-bit mono PCM decoded by the core for the platform player. */
public final class SoundPcm {
    public final int sampleRate;
    public final short[] samples;
    public final String message;

    public SoundPcm(int sampleRate, short[] samples, String message) {
        this.sampleRate = sampleRate;
        this.samples = samples == null ? new short[0] : samples;
        this.message = message == null ? "" : message;
    }
}
