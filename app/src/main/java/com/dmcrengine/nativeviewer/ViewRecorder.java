package com.dmcrengine.nativeviewer;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.graphics.Canvas;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.media.MediaMuxer;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.provider.MediaStore;
import android.view.Surface;

import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Locale;
import java.util.function.Consumer;

/**
 * Records what the view shows into an MP4 (H.264, no sound): Movies/DMC
 * Native Reader on Android 10+, else the app's movie folder. The frames are
 * drawn on the main thread at 30 a second; a worker thread writes them out.
 */
final class ViewRecorder {
    /** Draws one frame of the view at the given size. */
    interface Source {
        void drawCapture(Canvas canvas, int width, int height);
    }

    private static final int FRAME_MS = 33;
    private static final int LONG_SIDE = 1280;

    private final Handler main = new Handler(Looper.getMainLooper());
    private final ContentResolver resolver;
    private final Source source;
    private final MediaCodec codec;
    private final Surface surface;
    private final MediaMuxer muxer;
    private final ParcelFileDescriptor descriptor;
    private final Uri uri;
    private final String where;
    private final int width;
    private final int height;
    private Thread drain;
    private boolean running;
    private volatile int samples;

    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (!running) return;
            frame();
            main.postDelayed(this, FRAME_MS);
        }
    };

    private ViewRecorder(ContentResolver resolver, Source source, MediaCodec codec, Surface surface, MediaMuxer muxer,
                         ParcelFileDescriptor descriptor, Uri uri, String where, int width, int height) {
        this.resolver = resolver;
        this.source = source;
        this.codec = codec;
        this.surface = surface;
        this.muxer = muxer;
        this.descriptor = descriptor;
        this.uri = uri;
        this.where = where;
        this.width = width;
        this.height = height;
    }

    /** Starts a recording of a view this big; throws when the device cannot. */
    static ViewRecorder start(Context context, int viewWidth, int viewHeight, Source source) throws IOException {
        // The long side at most 1280, both sides a multiple of 16 (what every encoder takes).
        final float scale = Math.min(1.0f, (float) LONG_SIDE / Math.max(1, Math.max(viewWidth, viewHeight)));
        final int width = Math.max(16, Math.round(viewWidth * scale) / 16 * 16);
        final int height = Math.max(16, Math.round(viewHeight * scale) / 16 * 16);
        final String name = "dmc-" + new java.text.SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US)
                .format(new java.util.Date()) + ".mp4";
        final ContentResolver resolver = context.getContentResolver();
        Uri uri = null;
        ParcelFileDescriptor descriptor = null;
        MediaMuxer muxer;
        String where;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            final ContentValues values = new ContentValues();
            values.put(MediaStore.Video.Media.DISPLAY_NAME, name);
            values.put(MediaStore.Video.Media.MIME_TYPE, "video/mp4");
            values.put(MediaStore.Video.Media.RELATIVE_PATH, "Movies/DMC Native Reader");
            values.put(MediaStore.Video.Media.IS_PENDING, 1);
            uri = resolver.insert(MediaStore.Video.Media.EXTERNAL_CONTENT_URI, values);
            if (uri == null) throw new IOException("no place for the video");
            descriptor = resolver.openFileDescriptor(uri, "rw");
            if (descriptor == null) {
                resolver.delete(uri, null, null);
                throw new IOException("no place for the video");
            }
            muxer = new MediaMuxer(descriptor.getFileDescriptor(), MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4);
            where = "Movies/DMC Native Reader/" + name;
        } else {
            final File dir = context.getExternalFilesDir(Environment.DIRECTORY_MOVIES);
            if (dir == null || !(dir.isDirectory() || dir.mkdirs())) throw new IOException("no place for the video");
            final File file = new File(dir, name);
            muxer = new MediaMuxer(file.getPath(), MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4);
            where = file.getPath();
        }
        MediaCodec codec = null;
        try {
            final MediaFormat format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height);
            format.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);
            format.setInteger(MediaFormat.KEY_BIT_RATE, Math.max(2_000_000, width * height * 8));
            format.setInteger(MediaFormat.KEY_FRAME_RATE, 30);
            format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1);
            codec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC);
            codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            final Surface surface = codec.createInputSurface();
            codec.start();
            final ViewRecorder recorder = new ViewRecorder(resolver, source, codec, surface, muxer, descriptor, uri,
                    where, width, height);
            recorder.begin();
            return recorder;
        } catch (IOException | RuntimeException error) {
            if (codec != null) codec.release();
            muxer.release();
            if (descriptor != null) descriptor.close();
            if (uri != null) resolver.delete(uri, null, null);
            throw error instanceof IOException ? (IOException) error : new IOException(error);
        }
    }

    private void begin() {
        running = true;
        drain = new Thread(this::drainLoop, "view-recorder");
        drain.start();
        main.post(tick);
    }

    /** Draws the view into the next frame of the video. */
    private void frame() {
        try {
            final Canvas canvas = surface.lockHardwareCanvas();
            try {
                canvas.drawColor(0xff000000);
                source.drawCapture(canvas, width, height);
            } finally {
                surface.unlockCanvasAndPost(canvas);
            }
        } catch (RuntimeException ignored) {
            // A frame the encoder could not take: the next one follows.
        }
    }

    /** Ends the recording; done gets where it was saved, or null when nothing was written. */
    void stop(Consumer<String> done) {
        if (!running) return;
        running = false;
        main.removeCallbacks(tick);
        try {
            codec.signalEndOfInputStream();
        } catch (RuntimeException ignored) {
            // The worker below still closes the file.
        }
        new Thread(() -> {
            try {
                drain.join(5000);
            } catch (InterruptedException ignored) {
                Thread.currentThread().interrupt();
            }
            final String saved = finish();
            main.post(() -> done.accept(saved));
        }, "view-recorder-stop").start();
    }

    private void drainLoop() {
        final MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        int track = -1;
        long first = -1L;
        long idleSince = 0L;
        while (true) {
            final int index;
            try {
                index = codec.dequeueOutputBuffer(info, 10_000);
            } catch (RuntimeException error) {
                return;
            }
            if (index == MediaCodec.INFO_TRY_AGAIN_LATER) {
                // After the end was asked for, give the encoder two seconds at most.
                if (!running) {
                    if (idleSince == 0L) idleSince = System.currentTimeMillis();
                    else if (System.currentTimeMillis() - idleSince > 2000L) return;
                }
                continue;
            }
            idleSince = 0L;
            if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                if (track < 0) {
                    track = muxer.addTrack(codec.getOutputFormat());
                    muxer.start();
                }
                continue;
            }
            if (index < 0) continue;
            final ByteBuffer data = codec.getOutputBuffer(index);
            if ((info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) info.size = 0;
            if (data != null && info.size > 0 && track >= 0) {
                if (first < 0L) first = info.presentationTimeUs;
                info.presentationTimeUs = Math.max(0L, info.presentationTimeUs - first);
                data.position(info.offset);
                data.limit(info.offset + info.size);
                muxer.writeSampleData(track, data, info);
                samples++;
            }
            codec.releaseOutputBuffer(index, false);
            if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) return;
        }
    }

    private String finish() {
        boolean written = samples > 0;
        try {
            codec.stop();
        } catch (RuntimeException ignored) {
            // Already stopped by an error.
        }
        codec.release();
        surface.release();
        try {
            if (written) muxer.stop();
        } catch (RuntimeException error) {
            written = false;
        }
        muxer.release();
        try {
            if (descriptor != null) descriptor.close();
        } catch (IOException ignored) {
            // The file is complete; only the handle failed to close.
        }
        if (uri != null) {
            if (written) {
                final ContentValues values = new ContentValues();
                values.put(MediaStore.Video.Media.IS_PENDING, 0);
                resolver.update(uri, values, null, null);
            } else {
                resolver.delete(uri, null, null);
            }
        } else if (!written) {
            new File(where).delete();
        }
        return written ? where : null;
    }
}
