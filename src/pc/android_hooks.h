/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Platform hooks the LAN lobby needs from the Android Java side
 * (src/pc/android_compat.cpp). Every one of these is a no-op, or returns
 * NULL, on every other platform, so callers need no #ifdef. */
#ifndef PC_ANDROID_HOOKS_H
#define PC_ANDROID_HOOKS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hold a WifiManager.MulticastLock while LAN discovery runs. Without it the
 * Wi-Fi stack drops every multicast frame not addressed to this device, so
 * the mDNS group is joined, our announces go out, and no answer ever comes
 * back: pc_lan_discovery_unavailable() turns true after 5 s and the lobby
 * stays empty (docs/netcode-plan.md §8).
 *
 * Nesting is counted: the OS lock is taken on the 0 -> 1 acquire and dropped
 * on the 1 -> 0 release, so repeated pc_lan_start()/pc_lan_stop() cycles in
 * one process are safe, as is the atexit(pc_lan_stop) path. An unbalanced
 * release is ignored rather than letting MulticastLock.release() throw.
 * Safe to call from any thread. */
void pc_android_multicast_lock_acquire(void);
void pc_android_multicast_lock_release(void);

/* The user's device name ("Sian's Pixel 7") reduced to a DNS label, or NULL
 * when there is none to be had. gethostname() is no use on Android: stock
 * devices leave the kernel hostname at "localhost", so every instance would
 * announce localhost-<id>._meleepc._udp.local. and claim localhost.local.
 * The result is cached; the buffer is owned by the callee. */
const char* pc_android_device_name(void);

/* Tell the Android touch overlay whether a RmlUi menu owns the screen.
 * The overlay is the only touch entry point, and its analog-stick capture
 * zone covers the lower-left quarter of the screen, so while the launcher is
 * up it swallowed the taps meant for "Choose disc" -- the launcher is
 * unreachable on a phone with no other input. Inert (all touches pass
 * through, nothing is drawn) while active. Safe to call from any thread. */
void pc_android_set_launcher_active(bool active);
/* Choose a disc image before the launcher runs, in a real Android dialog
 * (src/pc/main.c calls this before pc_launcher_run). The launcher is an RmlUi
 * page drawn on the SDL surface, so TalkBack cannot read it and nothing but a
 * sighted tap can move its focus: on a phone the disc has to be chosen
 * somewhere Android's own accessibility can reach. saved_disc is the image
 * launcher.cfg remembers, offered as a one-tap Start so a returning player
 * never has to pick again; an empty one only offers the picker. Writes the
 * chosen content:// URI or path into out and returns true when the player
 * chose one -- pc_launcher_run then boots it straight in -- and false when
 * they asked for the launcher instead or the dialog could not be shown, which
 * leaves every path as it was. False on every other platform. Safe to call
 * from the SDL thread: it blocks there until the dialog is answered, while
 * Android's UI thread runs it. */
bool pc_android_startup_disc(const char* saved_disc, char* out, size_t cap);
/* Open Android's document picker to save recent app logs without adb. */
void pc_android_export_logs(void);
/* Keep read access to a disc image the picker handed over as a content://
 * URI (MeleeActivity.takePersistableUri). A picker's grant lasts for the
 * session; the launcher remembers the URI across launches, so without this
 * the next start could not open the disc the player chose. A no-op on every
 * other platform, and on a URI no provider will persist. */
void pc_android_take_persistable_uri(const char* uri);

/* The folder the accessibility cues' sound files were unpacked into from the
 * APK (MeleeActivity.getA11ySoundsDir), with its trailing separator, or NULL
 * when they could not be unpacked. gethostpath-style: owned by the callee and
 * cached. Every other platform returns NULL and its cues read resources/. */
const char* pc_android_a11y_sounds_dir(void);
/* Apply the saved fullscreen preference to Android system bars. */
bool pc_android_set_fullscreen(bool fullscreen);

#ifdef __cplusplus
}
#endif

#endif
