package com.dmcrengine.nativeviewer;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;
import android.media.PlaybackParams;

/**
 * Streams a cutscene's sound while its timeline plays.
 *
 * The core owns the sound: which file plays from which frame, the mix and
 * the position of a timeline frame in it (NativeBridge.demoAudio*). This
 * class only feeds the platform's AudioTrack from that position on a worker
 * thread and stops it when playback stops.
 */
final class CutsceneAudio {
    private static final int CHUNK_MS = 100;

    private Thread worker;
    private volatile boolean running;

    /** Plays the session's cutscene sound from timeline `frame`; nothing when it has none. */
    synchronized void play(long session, float frame, float speed) {
        stop();
        if (session == 0) return;
        final int[] format = NativeBridge.demoAudioFormat(session);
        if (format == null || format.length < 2 || format[0] <= 0) return;
        final long start = NativeBridge.demoAudioPosition(session, frame);
        running = true;
        worker = new Thread(() -> stream(session, format[0], format[1], start, speed), "cutscene-audio");
        worker.start();
    }

    synchronized void stop() {
        running = false;
        final Thread old = worker;
        worker = null;
        if (old != null) {
            old.interrupt();
            try {
                old.join(300);
            } catch (InterruptedException ignored) {
                Thread.currentThread().interrupt();
            }
        }
    }

    private void stream(long session, int rate, int channels, long position, float speed) {
        final int channelMask = channels >= 2 ? AudioFormat.CHANNEL_OUT_STEREO : AudioFormat.CHANNEL_OUT_MONO;
        final int minimum = AudioTrack.getMinBufferSize(rate, channelMask, AudioFormat.ENCODING_PCM_16BIT);
        final int chunkFrames = Math.max(256, rate * CHUNK_MS / 1000);
        AudioTrack track = null;
        try {
            track = new AudioTrack.Builder()
                    .setAudioAttributes(new AudioAttributes.Builder()
                            .setUsage(AudioAttributes.USAGE_MEDIA)
                            .setContentType(AudioAttributes.CONTENT_TYPE_MOVIE)
                            .build())
                    .setAudioFormat(new AudioFormat.Builder()
                            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                            .setSampleRate(rate)
                            .setChannelMask(channelMask)
                            .build())
                    .setTransferMode(AudioTrack.MODE_STREAM)
                    .setBufferSizeInBytes(Math.max(minimum, chunkFrames * channels * 2 * 2))
                    .build();
            if (speed > 0.0f && speed != 1.0f) {
                try {
                    track.setPlaybackParams(new PlaybackParams().setSpeed(speed));
                } catch (IllegalArgumentException | IllegalStateException ignored) {
                    // The speed this device cannot play: the sound keeps its own.
                }
            }
            track.play();
            long at = position;
            while (running && !Thread.currentThread().isInterrupted()) {
                final short[] chunk = NativeBridge.demoAudioRead(session, at, chunkFrames);
                if (chunk == null || chunk.length == 0) break;
                int written = 0;
                while (running && written < chunk.length) {
                    final int n = track.write(chunk, written, chunk.length - written);
                    if (n <= 0) break;
                    written += n;
                }
                at += chunk.length / Math.max(1, channels);
            }
        } catch (RuntimeException ignored) {
            // No audio output: the cutscene plays silent.
        } finally {
            if (track != null) {
                try {
                    if (running) {
                        track.stop();  // the end: let the last chunk play out
                    } else {
                        track.pause();
                        track.flush();
                    }
                } catch (IllegalStateException ignored) {
                    // already stopped
                }
                track.release();
            }
        }
    }
}
