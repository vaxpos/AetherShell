/*
 * ═══════════════════════════════════════════════════════════════════════════
 * 🔎 Venom Basilisk - Search Module Header
 * ═══════════════════════════════════════════════════════════════════════════
 */

#ifndef SEARCH_H
#define SEARCH_H

#include "basilisk.h"

void search_init(void);
void search_perform(const gchar *query);
void search_clear_results(void);
void search_load_apps(void);
void search_free_apps(void);

#endif
