package dev.melee;

import android.content.Context;
import android.media.AudioAttributes;
import android.os.Handler;
import android.os.Looper;
import android.speech.tts.TextToSpeech;
import android.speech.tts.UtteranceProgressListener;
import android.speech.tts.Voice;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Set;
import java.util.concurrent.Callable;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Android's text-to-speech, for the accessibility layer in src/pc/a11y
 * (android_tts.cpp): one engine reads the menus and the match aloud, and one
 * engine per voice renders the two voices' lines to memory for the game's cue
 * engine to play. Prism, the screen reader library the Windows and macOS
 * builds use, ships no Android build, so this is the platform's own reader.
 *
 * <p>Android builds a TextToSpeech on a thread with a Looper and calls back on
 * it, so every engine is created on, and every engine call is made from, the
 * main thread: a native caller posts the work there and waits for the answer.
 * The one wait that cannot happen on the main thread is a render's, because
 * the engine reports the finished file on it; that wait belongs to the
 * calling thread, which is the voices' own worker.
 *
 * <p>The engines, the voice table and the reader's name are touched on the
 * main thread only. A native thread reads them through these methods, which
 * post and wait, so nothing here is locked but start() and stop(), and those
 * never hold their lock across a call that the main thread could re-enter.
 *
 * <p>Every public method is called from native code (JNI), never from the UI.
 */
public final class MeleeSpeech {

    /** What a render answers with: the file's samples, already floats. */
    public static final class Rendered {
        public float[] samples;
        public int rate;
        public int channels;
    }

    private static MeleeSpeech sInstance;

    public static synchronized MeleeSpeech get(Context context) {
        if (sInstance == null) {
            sInstance = new MeleeSpeech(context.getApplicationContext());
        }
        return sInstance;
    }

    private final Context mContext;
    private final Handler mMain = new Handler(Looper.getMainLooper());
    /* Serialises start() and stop(); never held while the main thread might
     * wait for it. */
    private final Object mStartLock = new Object();

    /* Main thread only. */
    private TextToSpeech mReader;
    private TextToSpeech[] mVoices = new TextToSpeech[0];
    private final List<Voice> mInstalled = new ArrayList<>();

    /* Read from any thread: written on the main thread. */
    private volatile boolean mReaderReady;
    private volatile String mEngineName = "";
    private volatile CountDownLatch mRenderDone;
    private volatile String mRenderId;
    /* Shared by the game thread (speak) and the voices' worker (render). */
    private final AtomicLong mUtterance = new AtomicLong();

    private MeleeSpeech(Context context) {
        mContext = context;
    }

    /**
     * Starts the reader and, when asked, the voices' engines. False when the
     * device has no text-to-speech engine: the game then plays without
     * speech, which its log records once.
     */
    public boolean start(final int voiceCount) {
        synchronized (mStartLock) {
            if (!mReaderReady) {
                final CountDownLatch created = new CountDownLatch(1);
                final AtomicBoolean ok = new AtomicBoolean(false);
                mMain.post(() -> {
                    mReader = new TextToSpeech(mContext, status -> {
                        if (status == TextToSpeech.SUCCESS && mReader != null) {
                            mReader.setAudioAttributes(accessibilityAudio());
                            mReader.setOnUtteranceProgressListener(listener());
                            ok.set(true);
                        }
                        created.countDown();
                    });
                });
                if (!await(created, 10) || !ok.get()) {
                    return false;
                }
                mReaderReady = true;
            }

            if (voiceCount > 0 && mVoices.length < voiceCount) {
                final CountDownLatch created = new CountDownLatch(voiceCount);
                mMain.post(() -> {
                    TextToSpeech[] voices = new TextToSpeech[voiceCount];
                    for (int i = 0; i < voiceCount; i++) {
                        // Counted down either way: an engine that will not
                        // start renders nothing, and the voices then fall back
                        // to the reader rather than waiting for it.
                        final TextToSpeech engine =
                                new TextToSpeech(mContext, status -> created.countDown());
                        if (engine != null) {
                            engine.setAudioAttributes(accessibilityAudio());
                            engine.setOnUtteranceProgressListener(listener());
                        }
                        voices[i] = engine;
                    }
                    mVoices = voices;
                });
                await(created, 15);
            }

            /* The name and the voice table are published before this returns:
             * the voices' open() lists the voices straight after it. */
            call(() -> {
                if (mReader != null) {
                    String engine = mReader.getDefaultEngine();
                    mEngineName = engine != null ? engine : "";
                    listVoices();
                }
                return Boolean.TRUE;
            });
            return mReaderReady;
        }
    }

    /** The engine behind the reader, for the speech log's backend line. */
    public String engineName() {
        return mEngineName;
    }

    /**
     * Speaks a line. interrupt cuts off whatever is being said, which is how
     * a line about something new replaces one about the last thing.
     */
    public boolean speak(final String text, final boolean interrupt) {
        if (!mReaderReady) {
            return false;
        }
        Boolean answered = call(() -> {
            if (mReader == null) {
                return Boolean.FALSE;
            }
            int mode = interrupt ? TextToSpeech.QUEUE_FLUSH : TextToSpeech.QUEUE_ADD;
            // No parameters: the engine speaks with the audio attributes set
            // at start, the accessibility stream the phone's speech volume
            // controls.
            return mReader.speak(text, mode, null, "melee-" + mUtterance.incrementAndGet())
                    == TextToSpeech.SUCCESS;
        });
        return answered != null && answered;
    }

    public String[] voiceNames() {
        List<String> names = call(() -> {
            List<String> listed = new ArrayList<>(mInstalled.size());
            for (Voice voice : mInstalled) {
                listed.add(voice.getName());
            }
            return listed;
        });
        return names == null ? new String[0] : names.toArray(new String[0]);
    }

    public String[] voiceLanguages() {
        List<String> languages = call(() -> {
            List<String> listed = new ArrayList<>(mInstalled.size());
            for (Voice voice : mInstalled) {
                listed.add(voice.getLocale().toLanguageTag());
            }
            return listed;
        });
        return languages == null ? new String[0] : languages.toArray(new String[0]);
    }

    /** The reader's own default voice, as an index into the voices above. */
    public int defaultVoice() {
        Integer index = call(() -> {
            if (mReader == null) {
                return -1;
            }
            Voice current = mReader.getDefaultVoice();
            return current == null ? -1 : indexOfVoice(current.getName());
        });
        return index == null ? -1 : index;
    }

    public boolean setVoice(final int instance, final int index) {
        if (!mReaderReady) {
            return false;
        }
        Boolean answered = call(() -> {
            Voice voice = index >= 0 && index < mInstalled.size() ? mInstalled.get(index) : null;
            TextToSpeech engine = engine(instance);
            return voice != null && engine != null
                    && engine.setVoice(voice) == TextToSpeech.SUCCESS;
        });
        return answered != null && answered;
    }

    /** rate is 0 to 1 over the settings, 0.5 the engine's normal speed. */
    public boolean setRate(final int instance, final float rate) {
        Boolean answered = call(() -> {
            TextToSpeech engine = engine(instance);
            return engine != null && engine.setSpeechRate(rate * 2.0f) == TextToSpeech.SUCCESS;
        });
        return answered != null && answered;
    }

    /**
     * Synthesizes a line to a file and reads it back as floats. Null when the
     * device's engine will not synthesize to a file, which leaves the voices
     * unavailable so that their lines go to the reader instead.
     */
    public Rendered render(final int instance, final String text) {
        if (!mReaderReady) {
            return null;
        }
        final File file = new File(mContext.getCacheDir(), "a11y-voice.wav");
        final String id = "render-" + mUtterance.incrementAndGet();
        final CountDownLatch done = new CountDownLatch(1);
        mRenderDone = done;
        mRenderId = id;
        Boolean queued = call(() -> {
            TextToSpeech engine = engine(instance);
            if (engine == null) {
                return Boolean.FALSE;
            }
            // The listener matches this id, so a late callback for an older
            // line cannot release this one.
            return engine.synthesizeToFile(text, null, file, id) == TextToSpeech.SUCCESS;
        });
        if (queued == null || !queued) {
            return null;
        }
        if (!await(done, 10)) {
            return null;
        }
        try {
            return readWav(file);
        } catch (IOException e) {
            return null;
        }
    }

    public void stop() {
        synchronized (mStartLock) {
            final CountDownLatch closed = new CountDownLatch(1);
            mMain.post(() -> {
                if (mReader != null) {
                    mReader.stop();
                    mReader.shutdown();
                    mReader = null;
                }
                for (TextToSpeech engine : mVoices) {
                    if (engine != null) {
                        engine.stop();
                        engine.shutdown();
                    }
                }
                mVoices = new TextToSpeech[0];
                mInstalled.clear();
                mReaderReady = false;
                closed.countDown();
            });
            await(closed, 5);
        }
    }

    private AudioAttributes accessibilityAudio() {
        return new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_ASSISTANCE_ACCESSIBILITY)
                .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                .build();
    }

    private UtteranceProgressListener listener() {
        return new UtteranceProgressListener() {
            @Override
            public void onStart(String utteranceId) {}

            @Override
            public void onDone(String utteranceId) {
                finish(utteranceId);
            }

            @Override
            public void onError(String utteranceId) {
                finish(utteranceId);
            }

            @Override
            public void onError(String utteranceId, int errorCode) {
                finish(utteranceId);
            }
        };
    }

    private void finish(String utteranceId) {
        CountDownLatch done = mRenderDone;
        String id = mRenderId;
        if (done != null && id != null && id.equals(utteranceId)) {
            done.countDown();
        }
    }

    /** Main thread only, after the reader is ready. */
    private void listVoices() {
        if (mReader == null) {
            return;
        }
        Set<Voice> installed = mReader.getVoices();
        mInstalled.clear();
        if (installed == null) {
            return;
        }
        for (Voice voice : installed) {
            Set<String> features = voice.getFeatures();
            if (features != null
                    && features.contains(TextToSpeech.Engine.KEY_FEATURE_NOT_INSTALLED)) {
                continue;
            }
            mInstalled.add(voice);
        }
        // The table the settings' saved voice text is matched against, by
        // language and then name, so an index means the same voice twice in
        // one run and the Accessibility rows read in a sensible order.
        Collections.sort(mInstalled, new Comparator<Voice>() {
            @Override
            public int compare(Voice a, Voice b) {
                int language = a.getLocale().toLanguageTag()
                        .compareTo(b.getLocale().toLanguageTag());
                return language != 0 ? language : a.getName().compareTo(b.getName());
            }
        });
    }

    /** Main thread only. */
    private int indexOfVoice(String name) {
        for (int i = 0; i < mInstalled.size(); i++) {
            if (mInstalled.get(i).getName().equals(name)) {
                return i;
            }
        }
        return -1;
    }

    /** Main thread only, like every reader of mVoices. */
    private TextToSpeech engine(int instance) {
        TextToSpeech[] voices = mVoices;
        if (instance < 0 || instance >= voices.length) {
            return null;
        }
        return voices[instance];
    }

    /**
     * Runs a task on the main thread and waits for its answer. On the main
     * thread itself it runs in place, since a thread cannot wait for itself.
     */
    private <T> T call(Callable<T> task) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            try {
                return task.call();
            } catch (Exception e) {
                return null;
            }
        }
        FutureTask<T> future = new FutureTask<>(task);
        mMain.post(future);
        try {
            return future.get(10, TimeUnit.SECONDS);
        } catch (Exception e) {
            return null;
        }
    }

    private static boolean await(CountDownLatch latch, int seconds) {
        try {
            return latch.await(seconds, TimeUnit.SECONDS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return false;
        }
    }

    /**
     * A PCM WAV, as Android's engines write one: 16-bit samples at the
     * engine's own rate, mono or stereo. Null for anything else, which leaves
     * the voices to the reader.
     */
    private static Rendered readWav(File file) throws IOException {
        if (!file.isFile() || file.length() < 44) {
            return null;
        }
        byte[] bytes = new byte[(int) Math.min(file.length(), 8 * 1024 * 1024)];
        int read = 0;
        try (FileInputStream in = new FileInputStream(file)) {
            while (read < bytes.length) {
                int count = in.read(bytes, read, bytes.length - read);
                if (count < 0) {
                    break;
                }
                read += count;
            }
        }
        if (read < 44) {
            return null;
        }
        if (read < bytes.length) {
            byte[] shorter = new byte[read];
            System.arraycopy(bytes, 0, shorter, 0, read);
            bytes = shorter;
        }
        ByteBuffer buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        if (buffer.getInt(0) != 0x46464952 || buffer.getInt(8) != 0x45564157) {
            return null; /* not "RIFF" ... "WAVE" */
        }
        int format = 1;
        int channels = 1;
        int rate = 0;
        int bits = 16;
        int dataOffset = -1;
        int dataLength = 0;
        int offset = 12;
        while (offset + 8 <= bytes.length) {
            int id = buffer.getInt(offset);
            int size = buffer.getInt(offset + 4);
            int body = offset + 8;
            if (size < 0) {
                break;
            }
            if (id == 0x20746d66 && body + 16 <= bytes.length) { /* "fmt " */
                format = buffer.getShort(body) & 0xFFFF;
                channels = buffer.getShort(body + 2) & 0xFFFF;
                rate = buffer.getInt(body + 4);
                bits = buffer.getShort(body + 14) & 0xFFFF;
            } else if (id == 0x61746164) { /* "data" */
                dataOffset = body;
                dataLength = Math.min(size, bytes.length - body);
                break;
            }
            offset = body + size + (size & 1);
        }
        if (dataOffset < 0 || dataLength <= 0 || format != 1 || bits != 16 || channels <= 0
                || rate <= 0) {
            return null;
        }
        int frames = dataLength / (2 * channels);
        if (frames <= 0) {
            return null;
        }
        Rendered rendered = new Rendered();
        rendered.samples = new float[frames * channels];
        rendered.rate = rate;
        rendered.channels = channels;
        int at = dataOffset;
        for (int i = 0; i < rendered.samples.length; i++) {
            rendered.samples[i] = buffer.getShort(at) / 32768.0f;
            at += 2;
        }
        return rendered;
    }
}
