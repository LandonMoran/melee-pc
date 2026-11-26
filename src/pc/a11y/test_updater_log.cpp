/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The base port's updater test (tools/test_updater.cpp) compiles updater.cpp,
 * whose update check calls the fork's hand-offs in update_hooks.cpp; they log
 * through the pc_log_line that main.c gives the game, silent here. */
extern "C" void pc_log_line(const char*, ...) {}
