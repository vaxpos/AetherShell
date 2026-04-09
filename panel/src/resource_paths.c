#include "resource_paths.h"

#include <glib.h>

#ifndef VAXPWY_PANEL_RESOURCES_DIR
#define VAXPWY_PANEL_RESOURCES_DIR "/usr/local/share/panel/resources"
#endif

#define VAXPWY_PANEL_RESOURCES_DIR_FALLBACK "/usr/share/panel/resources"

static char *get_binary_dir(void)
{
    char *binary_path = g_file_read_link("/proc/self/exe", NULL);
    if (!binary_path)
    {
        return g_get_current_dir();
    }

    char *binary_dir = g_path_get_dirname(binary_path);
    g_free(binary_path);
    return binary_dir;
}

static char *resolve_candidate(const char *base, const char *subdir, const char *filename)
{
    char *candidate = subdir ?
        g_build_filename(base, subdir, filename, NULL) :
        g_build_filename(base, filename, NULL);

    if (g_file_test(candidate, G_FILE_TEST_EXISTS))
    {
        return candidate;
    }

    g_free(candidate);
    return NULL;
}

char *panel_resource_path_in(const char *subdir, const char *filename)
{
    char *candidate = NULL;
    char *cwd = g_get_current_dir();
    char *binary_dir = get_binary_dir();

    candidate = resolve_candidate(cwd, subdir, filename);
    if (candidate)
    {
        g_free(cwd);
        g_free(binary_dir);
        return candidate;
    }

    candidate = subdir ?
        g_build_filename(cwd, "panel", subdir, filename, NULL) :
        g_build_filename(cwd, "panel", filename, NULL);
    if (g_file_test(candidate, G_FILE_TEST_EXISTS))
    {
        g_free(cwd);
        g_free(binary_dir);
        return candidate;
    }
    g_free(candidate);

    candidate = resolve_candidate(binary_dir, "resources", filename);
    if (candidate)
    {
        g_free(cwd);
        g_free(binary_dir);
        return candidate;
    }

    candidate = subdir ?
        g_build_filename(binary_dir, "resources", subdir, filename, NULL) :
        g_build_filename(binary_dir, "resources", filename, NULL);
    if (g_file_test(candidate, G_FILE_TEST_EXISTS))
    {
        g_free(cwd);
        g_free(binary_dir);
        return candidate;
    }
    g_free(candidate);

    candidate = subdir ?
        g_build_filename(VAXPWY_PANEL_RESOURCES_DIR, subdir, filename, NULL) :
        g_build_filename(VAXPWY_PANEL_RESOURCES_DIR, filename, NULL);
    if (g_file_test(candidate, G_FILE_TEST_EXISTS))
    {
        g_free(cwd);
        g_free(binary_dir);
        return candidate;
    }
    g_free(candidate);

    g_free(cwd);
    g_free(binary_dir);

    return subdir ?
        g_build_filename(VAXPWY_PANEL_RESOURCES_DIR_FALLBACK, subdir, filename, NULL) :
        g_build_filename(VAXPWY_PANEL_RESOURCES_DIR_FALLBACK, filename, NULL);
}

char *panel_resource_path(const char *filename)
{
    return panel_resource_path_in(NULL, filename);
}
