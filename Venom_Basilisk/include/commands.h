/*
 * ═══════════════════════════════════════════════════════════════════════════
 * ⚡ Venom Basilisk - Commands Module Header
 * ═══════════════════════════════════════════════════════════════════════════
 */

#ifndef COMMANDS_H
#define COMMANDS_H

#include "basilisk.h"

// Command prefixes
#define CMD_VATER   "vater:"
#define CMD_MATH    "!:"
#define CMD_FILE    "vafile:"
#define CMD_GITHUB  "g:"
#define CMD_GOOGLE  "s:"
#define CMD_AI      "ai:"

gboolean commands_check_prefix(const gchar *query);
void commands_execute(const gchar *query);
void commands_execute_vater(const gchar *cmd);
void commands_execute_math(const gchar *expr);
void commands_execute_file_search(const gchar *term);
void commands_execute_web_search(const gchar *term, const gchar *engine);

#endif
