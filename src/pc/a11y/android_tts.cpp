/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Android text-to-speech through JNI (android_tts.hpp). The Java side is
 * platforms/android/app/src/main/java/dev/melee/MeleeSpeech.java, reached
 * through MeleeActivity.getSpeech(), which is how a native thread gets at an
 * application class: FindClass on an attached thread would look in the system
 * class loader, where dev.melee.MeleeSpeech is not.
 *
 * Every signature below is the Java file's, and the calls are made on the
 * thread that asks: Java posts them to the main looper and answers when they
 * are done, so the game thread and the voices' worker never touch the same
 * engine at once. The global reference and the ids are found once, under
 * g_init; nothing else is locked, because a render takes as long as the
 * engine needs and must not hold up a screen reader line. */
#include "android_tts.hpp"
#include "pc/pc.h"
#include <SDL3/SDL_system.h>
#include <jni.h>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace a11y::android {
namespace {

/* Guards the lookup of the Java object and its ids, and nothing else. */
std::mutex g_init;
jobject g_speech;        /* dev.melee.MeleeSpeech, a global reference */
jmethodID g_start;       /* (I)Z */
jmethodID g_stop;        /* ()V */
jmethodID g_engine_name; /* ()Ljava/lang/String; */
jmethodID g_speak;       /* (Ljava/lang/String;Z)Z */
jmethodID g_voice_names; /* ()[Ljava/lang/String; */
jmethodID g_voice_languages; /* ()[Ljava/lang/String; */
jmethodID g_default_voice;   /* ()I */
jmethodID g_set_voice;       /* (II)Z */
jmethodID g_set_rate;        /* (IF)Z */
jmethodID g_render; /* (ILjava/lang/String;)Ldev/melee/MeleeSpeech$Rendered; */
jfieldID g_samples;  /* Rendered.samples: [F */
jfieldID g_rate;     /* Rendered.rate: I */
jfieldID g_channels; /* Rendered.channels: I */
std::string g_engine_name_text;
bool g_ready;
/* How many voice engines Java has made: the reader starts first (speech),
 * the voices later (voices.cpp), so a start that only repeats the reader's
 * zero must not stand in for the engines the voices need. */
int g_instances;
bool g_lookup_failed;

/* True, and the exception cleared, when the last call left one pending: a
 * missing method or a Java failure is logged once as a line, not as a
 * trace, since play continues without speech. */
bool threw(JNIEnv* env, const char* what) {
    if (env->ExceptionCheck() == JNI_FALSE) {
        return false;
    }
    env->ExceptionClear();
    pc_log_line("[a11y] android: %s failed", what);
    return true;
}

/* SDL attaches the calling native thread to the Java VM the first time it
 * asks, which is what the voices' worker needs. */
JNIEnv* env_for_thread() {
    return static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
}

std::string from_java(JNIEnv* env, jstring text) {
    if (text == nullptr) {
        return {};
    }
    const char* utf = env->GetStringUTFChars(text, nullptr);
    std::string out = utf != nullptr ? utf : "";
    if (utf != nullptr) {
        env->ReleaseStringUTFChars(text, utf);
    }
    return out;
}

/* The Java object, made once. Called under g_init. A local frame keeps the
 * activity and the class refs from piling up on a thread that lives as long
 * as the process. */
jobject speech_locked(JNIEnv* env) {
    if (g_speech != nullptr || g_lookup_failed) {
        return g_speech;
    }
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (activity == nullptr) {
        g_lookup_failed = true;
        pc_log_line("[a11y] android: no activity, so no speech");
        return nullptr;
    }
    if (env->PushLocalFrame(16) != JNI_OK) {
        g_lookup_failed = true;
        return nullptr;
    }
    jobject speech = nullptr;
    do {
        jclass activity_class = env->GetObjectClass(activity);
        if (activity_class == nullptr) {
            break;
        }
        jmethodID get_speech =
            env->GetMethodID(activity_class, "getSpeech", "()Ldev/melee/MeleeSpeech;");
        if (get_speech == nullptr) {
            threw(env, "MeleeActivity.getSpeech");
            break;
        }
        speech = env->CallObjectMethod(activity, get_speech);
        if (threw(env, "getSpeech") || speech == nullptr) {
            speech = nullptr;
            break;
        }
        jclass speech_class = env->GetObjectClass(speech);
        if (speech_class == nullptr) {
            speech = nullptr;
            break;
        }
        g_start = env->GetMethodID(speech_class, "start", "(I)Z");
        g_stop = env->GetMethodID(speech_class, "stop", "()V");
        g_engine_name =
            env->GetMethodID(speech_class, "engineName", "()Ljava/lang/String;");
        g_speak = env->GetMethodID(speech_class, "speak", "(Ljava/lang/String;Z)Z");
        g_voice_names =
            env->GetMethodID(speech_class, "voiceNames", "()[Ljava/lang/String;");
        g_voice_languages =
            env->GetMethodID(speech_class, "voiceLanguages", "()[Ljava/lang/String;");
        g_default_voice = env->GetMethodID(speech_class, "defaultVoice", "()I");
        g_set_voice = env->GetMethodID(speech_class, "setVoice", "(II)Z");
        g_set_rate = env->GetMethodID(speech_class, "setRate", "(IF)Z");
        g_render = env->GetMethodID(speech_class, "render",
            "(ILjava/lang/String;)Ldev/melee/MeleeSpeech$Rendered;");
        if (g_start == nullptr || g_stop == nullptr || g_engine_name == nullptr ||
            g_speak == nullptr || g_voice_names == nullptr || g_voice_languages == nullptr ||
            g_default_voice == nullptr || g_set_voice == nullptr || g_set_rate == nullptr ||
            g_render == nullptr)
        {
            threw(env, "the MeleeSpeech methods");
            speech = nullptr;
            break;
        }
        /* Rendered, the nested class render answers with, is read from the
         * object it hands back (render below), never found by name here: a
         * native thread's FindClass looks in the system class loader. */
        g_speech = env->NewGlobalRef(speech);
        if (g_speech == nullptr) {
            speech = nullptr;
            break;
        }
    } while (false);
    env->PopLocalFrame(nullptr);
    if (g_speech == nullptr) {
        g_lookup_failed = true;
        pc_log_line("[a11y] android: MeleeSpeech is not there, so no speech");
    }
    return g_speech;
}

}  // namespace

bool start(int instances) {
    std::lock_guard<std::mutex> guard(g_init);
    JNIEnv* env = env_for_thread();
    if (env == nullptr || speech_locked(env) == nullptr) {
        return false;
    }
    /* Java adds the engines a later start asks for and leaves the ones it
     * already has, so a repeat is cheap and harmless. */
    if (g_ready && g_instances >= instances) {
        return true;
    }
    const jboolean ok = env->CallBooleanMethod(g_speech, g_start, static_cast<jint>(instances));
    if (threw(env, "MeleeSpeech.start") || ok == JNI_FALSE) {
        return false;
    }
    if (instances > g_instances) {
        g_instances = instances;
    }
    jstring name = static_cast<jstring>(env->CallObjectMethod(g_speech, g_engine_name));
    if (!threw(env, "engineName")) {
        g_engine_name_text = from_java(env, name);
    }
    if (name != nullptr) {
        env->DeleteLocalRef(name);
    }
    g_ready = true;
    return true;
}

void stop() {
    std::lock_guard<std::mutex> guard(g_init);
    if (!g_ready) {
        return;
    }
    g_ready = false;
    g_instances = 0;
    JNIEnv* env = env_for_thread();
    if (env == nullptr || g_speech == nullptr) {
        return;
    }
    env->CallVoidMethod(g_speech, g_stop);
    threw(env, "MeleeSpeech.stop");
}

std::string engine_name() {
    std::lock_guard<std::mutex> guard(g_init);
    return g_engine_name_text.empty() ? std::string("Android TextToSpeech") :
                                        "Android TextToSpeech (" + g_engine_name_text + ")";
}

bool speak(std::string_view utf8, bool interrupt) {
    JNIEnv* env = env_for_thread();
    if (env == nullptr || g_speech == nullptr || !g_ready) {
        return false;
    }
    const std::string text(utf8);
    jstring line = env->NewStringUTF(text.c_str());
    if (line == nullptr) {
        threw(env, "NewStringUTF");
        return false;
    }
    const jboolean ok =
        env->CallBooleanMethod(g_speech, g_speak, line, interrupt ? JNI_TRUE : JNI_FALSE);
    env->DeleteLocalRef(line);
    return !threw(env, "MeleeSpeech.speak") && ok != JNI_FALSE;
}

std::vector<VoiceInfo> voices() {
    std::vector<VoiceInfo> listed;
    JNIEnv* env = env_for_thread();
    if (env == nullptr || g_speech == nullptr || !g_ready) {
        return listed;
    }
    if (env->PushLocalFrame(8) != JNI_OK) {
        return listed;
    }
    auto names = static_cast<jobjectArray>(env->CallObjectMethod(g_speech, g_voice_names));
    auto languages = static_cast<jobjectArray>(env->CallObjectMethod(g_speech, g_voice_languages));
    if (!threw(env, "the voice lists") && names != nullptr) {
        const jsize count = env->GetArrayLength(names);
        const jsize languages_count =
            languages != nullptr ? env->GetArrayLength(languages) : static_cast<jsize>(0);
        for (jsize i = 0; i < count; i++) {
            VoiceInfo info;
            auto name = static_cast<jstring>(env->GetObjectArrayElement(names, i));
            info.name = from_java(env, name);
            if (name != nullptr) {
                env->DeleteLocalRef(name);
            }
            if (i < languages_count) {
                auto language = static_cast<jstring>(env->GetObjectArrayElement(languages, i));
                info.language = from_java(env, language);
                if (language != nullptr) {
                    env->DeleteLocalRef(language);
                }
            }
            listed.push_back(std::move(info));
        }
    }
    env->PopLocalFrame(nullptr);
    return listed;
}

int default_voice() {
    JNIEnv* env = env_for_thread();
    if (env == nullptr || g_speech == nullptr || !g_ready) {
        return -1;
    }
    const jint index = env->CallIntMethod(g_speech, g_default_voice);
    return threw(env, "defaultVoice") ? -1 : static_cast<int>(index);
}

bool set_voice(int instance, int index) {
    JNIEnv* env = env_for_thread();
    if (env == nullptr || g_speech == nullptr || !g_ready) {
        return false;
    }
    const jboolean ok =
        env->CallBooleanMethod(g_speech, g_set_voice, static_cast<jint>(instance),
            static_cast<jint>(index));
    return !threw(env, "setVoice") && ok != JNI_FALSE;
}

bool set_rate(int instance, float rate) {
    JNIEnv* env = env_for_thread();
    if (env == nullptr || g_speech == nullptr || !g_ready) {
        return false;
    }
    const jboolean ok = env->CallBooleanMethod(g_speech, g_set_rate,
        static_cast<jint>(instance), static_cast<jfloat>(rate));
    return !threw(env, "setRate") && ok != JNI_FALSE;
}

bool render(int instance, std::string_view utf8, RenderedLine& out) {
    out = RenderedLine{};
    JNIEnv* env = env_for_thread();
    if (env == nullptr || g_speech == nullptr || !g_ready) {
        return false;
    }
    const std::string text(utf8);
    jstring line = env->NewStringUTF(text.c_str());
    if (line == nullptr) {
        threw(env, "NewStringUTF");
        return false;
    }
    const jobject rendered =
        env->CallObjectMethod(g_speech, g_render, static_cast<jint>(instance), line);
    env->DeleteLocalRef(line);
    if (threw(env, "MeleeSpeech.render") || rendered == nullptr) {
        return false;
    }
    bool ok = false;
    jclass rendered_class = env->GetObjectClass(rendered);
    if (rendered_class != nullptr && g_samples == nullptr) {
        g_samples = env->GetFieldID(rendered_class, "samples", "[F");
        g_rate = env->GetFieldID(rendered_class, "rate", "I");
        g_channels = env->GetFieldID(rendered_class, "channels", "I");
        if (g_samples == nullptr || g_rate == nullptr || g_channels == nullptr) {
            threw(env, "the Rendered fields");
        }
    }
    if (g_samples != nullptr) {
        auto samples = static_cast<jfloatArray>(env->GetObjectField(rendered, g_samples));
        const jint rate = env->GetIntField(rendered, g_rate);
        const jint channels = env->GetIntField(rendered, g_channels);
        if (samples != nullptr) {
            const jsize count = env->GetArrayLength(samples);
            if (count > 0) {
                out.samples.resize(static_cast<size_t>(count));
                env->GetFloatArrayRegion(samples, 0, count, out.samples.data());
                out.rate = static_cast<int>(rate);
                out.channels = static_cast<int>(channels);
                ok = !threw(env, "the samples");
            }
            env->DeleteLocalRef(samples);
        }
    }
    if (rendered_class != nullptr) {
        env->DeleteLocalRef(rendered_class);
    }
    env->DeleteLocalRef(rendered);
    return ok;
}

}  // namespace a11y::android
