/*
 * sidebar.c — sidebar popup content
 *
 * One tall sidebar popup containing (top -> bottom):
 *   1. Digital Clock
 *   2. Date
 *   3. Monthly Calendar
 *   4. System Info
 *   5. RAM / Disk / VRAM
 *   6. Network
 *   7. System Monitor
 */
#include <gtk/gtk.h>
#include <gtk-layer-shell.h>
#include "window_backend.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/statvfs.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ═══════════════════════════════════════════════════════════════════
   SECTION 1 — CLOCK / DATE
   ═══════════════════════════════════════════════════════════════════ */
static GtkWidget *lbl_time = NULL;
static GtkWidget *lbl_date = NULL;

static gboolean update_clock(gpointer _) {
    if (!lbl_time || !lbl_date) return FALSE;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char ts[16], ds[64];
    strftime(ts, sizeof(ts), "%H:%M", t);
    strftime(ds, sizeof(ds), "%a, %d %B", t);
    gtk_label_set_text(GTK_LABEL(lbl_time), ts);
    gtk_label_set_text(GTK_LABEL(lbl_date), ds);
    return TRUE;
}

/* ═══════════════════════════════════════════════════════════════════
   SECTION 2 — CALENDAR
   ═══════════════════════════════════════════════════════════════════ */
static GtkWidget *cal_grid  = NULL;
static GtkWidget *lbl_month = NULL;
static GtkWidget *lbl_year  = NULL;
static int view_year = 0, view_month = 0;

static const char *MONTH_NAMES[] = {
    "January","February","March","April","May","June",
    "July","August","September","October","November","December"
};
static const char *DAY_ABBR[] = {"Su","Mo","Tu","We","Th","Fr","Sa"};

static int days_in_month(int y, int m) {
    int d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 1 && ((y%4==0&&y%100!=0)||y%400==0)) return 29;
    return d[m];
}
static int first_dow(int y, int m) {
    struct tm t = {0};
    t.tm_year = y - 1900; t.tm_mon = m; t.tm_mday = 1;
    mktime(&t); return t.tm_wday;
}

static void rebuild_calendar(void) {
    if (!cal_grid) return;
    GList *ch = gtk_container_get_children(GTK_CONTAINER(cal_grid));
    for (GList *l = ch; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(ch);

    char buf[40];
    snprintf(buf, sizeof(buf), " %s ", MONTH_NAMES[view_month]);
    gtk_label_set_text(GTK_LABEL(lbl_month), buf);
    snprintf(buf, sizeof(buf), " %d ", view_year);
    gtk_label_set_text(GTK_LABEL(lbl_year), buf);

    /* Day-of-week header */
    for (int c = 0; c < 7; c++) {
        GtkWidget *h = gtk_label_new(DAY_ABBR[c]);
        gtk_widget_set_name(h, "cal_hdr");
        gtk_widget_set_size_request(h, 28, 18);
        gtk_grid_attach(GTK_GRID(cal_grid), h, c, 0, 1, 1);
    }

    time_t now = time(NULL); struct tm *tnow = localtime(&now);
    int today = tnow->tm_mday, tcurm = tnow->tm_mon, tcury = tnow->tm_year+1900;

    int sow   = first_dow(view_year, view_month);
    int dcur  = days_in_month(view_year, view_month);
    int dprev = days_in_month(view_month==0?view_year-1:view_year,
                              view_month==0?11:view_month-1);

    int col = sow, row = 1, day = 1, trail = 1;

    /* leading fade days */
    for (int i = sow-1; i >= 0; i--) {
        GtkWidget *l = gtk_label_new(g_strdup_printf("%d", dprev-i));
        gtk_widget_set_name(l, "cal_fade");
        gtk_widget_set_size_request(l, 28, 20);
        gtk_label_set_xalign(GTK_LABEL(l), 0.5);
        gtk_grid_attach(GTK_GRID(cal_grid), l, sow-1-i, row, 1, 1);
    }
    /* current month */
    while (day <= dcur) {
        gboolean today_flag = (day==today && view_month==tcurm && view_year==tcury);
        GtkWidget *l = gtk_label_new(g_strdup_printf("%d", day));
        gtk_widget_set_name(l, today_flag ? "cal_today" : "cal_day");
        gtk_widget_set_size_request(l, 28, 20);
        gtk_label_set_xalign(GTK_LABEL(l), 0.5);
        gtk_grid_attach(GTK_GRID(cal_grid), l, col, row, 1, 1);
        col++; if (col==7){col=0; row++;} day++;
    }
    /* trailing fade */
    while (col > 0 && col < 7) {
        GtkWidget *l = gtk_label_new(g_strdup_printf("%d", trail++));
        gtk_widget_set_name(l, "cal_fade");
        gtk_widget_set_size_request(l, 28, 20);
        gtk_label_set_xalign(GTK_LABEL(l), 0.5);
        gtk_grid_attach(GTK_GRID(cal_grid), l, col++, row, 1, 1);
    }
    gtk_widget_show_all(cal_grid);
}

static void on_prev_month(GtkWidget *w, gpointer d) {
    if (--view_month < 0) { view_month=11; view_year--; } rebuild_calendar(); }
static void on_next_month(GtkWidget *w, gpointer d) {
    if (++view_month > 11){ view_month=0;  view_year++; } rebuild_calendar(); }
static void on_prev_year (GtkWidget *w, gpointer d) { view_year--; rebuild_calendar(); }
static void on_next_year (GtkWidget *w, gpointer d) { view_year++; rebuild_calendar(); }

/* ═══════════════════════════════════════════════════════════════════
   SECTION 3 — SYSTEM INFO
   ═══════════════════════════════════════════════════════════════════ */
static GtkWidget *lbl_compositor = NULL;
static GtkWidget *lbl_shell      = NULL;
static GtkWidget *lbl_uptime     = NULL;
static GtkWidget *lbl_packages   = NULL;

static char* run_cmd(const char *cmd) {
    FILE *f = popen(cmd, "r");
    if (!f) return g_strdup("N/A");
    char buf[256] = {0};
    if (!fgets(buf, sizeof(buf), f)) {
        pclose(f);
        return g_strdup("N/A");
    }
    pclose(f);
    size_t l = strlen(buf);
    if (l>0 && buf[l-1]=='\n') buf[l-1]='\0';
    return g_strdup(buf[0] ? buf : "N/A");
}
static char* get_compositor(void) {
    if (getenv("WAYLAND_DISPLAY")) {
        const char *desktop = getenv("XDG_CURRENT_DESKTOP");
        if (desktop && *desktop) return g_strdup(desktop);
        return g_strdup("Wayland");
    }
    return g_strdup("X11");
}
static char* get_shell(void) {
    const char *sh = getenv("SHELL");
    if (sh) { const char *b = strrchr(sh,'/'); return g_strdup(b?b+1:sh); }
    return run_cmd("basename $SHELL");
}
static char* get_uptime_str(void) {
    FILE *f = fopen("/proc/uptime","r");
    if (!f) return g_strdup("N/A");
    double s = 0;
    if (fscanf(f, "%lf", &s) != 1) {
        fclose(f);
        return g_strdup("N/A");
    }
    fclose(f);
    return g_strdup_printf("%dh %dm", (int)(s/3600), (int)((s-(int)(s/3600)*3600)/60));
}
static char* get_packages(void) {
    const char *cmds[] = {
        "pacman -Q 2>/dev/null | wc -l",
        "dpkg --list 2>/dev/null | grep '^ii' | wc -l",
        "rpm -qa 2>/dev/null | wc -l", NULL };
    for (int i=0; cmds[i]; i++) {
        char *r = run_cmd(cmds[i]);
        if (r && atoi(r)>0) { char *o=g_strdup_printf("%s ",r); g_free(r); return o; }
        g_free(r);
    }
    return g_strdup("N/A");
}

static gboolean refresh_sysinfo(gpointer _) {
    if (lbl_uptime)   { char *u=get_uptime_str(); gtk_label_set_text(GTK_LABEL(lbl_uptime),u);   g_free(u); }
    if (lbl_packages) { char *p=get_packages();   gtk_label_set_text(GTK_LABEL(lbl_packages),p); g_free(p); }
    return TRUE;
}

/* ═══════════════════════════════════════════════════════════════════
   SECTION 4 — RAM / DISK USAGE
   ═══════════════════════════════════════════════════════════════════ */
static GtkWidget *lbl_ram_usage  = NULL;
static GtkWidget *lbl_disk_usage = NULL;
static GtkWidget *lbl_vram_usage = NULL;
static GtkWidget *ram_bar_draw   = NULL;
static GtkWidget *disk_bar_draw  = NULL;
static GtkWidget *vram_bar_draw  = NULL;
static gulong ram_total_kb = 0;
static gulong ram_used_kb  = 0;
static gulong disk_total_gb = 0;
static gulong disk_used_gb  = 0;
static double vram_total_mb = 0;
static double vram_used_mb  = 0;

static void fmt_bytes(gulong kb, char *out, size_t len) {
    if (kb >= 1024 * 1024)
        snprintf(out, len, "%.1f GB", kb / (1024.0 * 1024.0));
    else if (kb >= 1024)
        snprintf(out, len, "%.0f MB", kb / 1024.0);
    else
        snprintf(out, len, "%lu KB", kb);
}

static void fmt_mb(double mb, char *out, size_t len) {
    if (mb >= 1024.0)
        snprintf(out, len, "%.1f GB", mb / 1024.0);
    else
        snprintf(out, len, "%.0f MB", mb);
}

static void read_ram_usage(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;

    gulong mem_total = 0, mem_free = 0, buffers = 0, cached = 0, sreclaimable = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        gulong val = 0;
        if      (sscanf(line, "MemTotal: %lu",     &val) == 1) mem_total    = val;
        else if (sscanf(line, "MemFree: %lu",      &val) == 1) mem_free     = val;
        else if (sscanf(line, "Buffers: %lu",      &val) == 1) buffers      = val;
        else if (sscanf(line, "Cached: %lu",       &val) == 1) cached       = val;
        else if (sscanf(line, "SReclaimable: %lu", &val) == 1) sreclaimable = val;
    }
    fclose(f);

    ram_total_kb = mem_total;
    ram_used_kb = mem_total - mem_free - buffers - cached - sreclaimable;
    if ((glong)ram_used_kb < 0) ram_used_kb = 0;
}

static void read_disk_usage(void) {
    struct statvfs sv;
    if (statvfs("/", &sv) != 0) return;
    gulong block = sv.f_bsize;
    disk_total_gb = (sv.f_blocks * block) / (1024 * 1024 * 1024UL);
    disk_used_gb  = ((sv.f_blocks - sv.f_bfree) * block) / (1024 * 1024 * 1024UL);
}

static long long read_ll_file_local(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    long long v = -1;
    if (fscanf(f, "%lld", &v) != 1) v = -1;
    fclose(f);
    return v;
}

/* ─── VRAM: read from sysfs only (no popen) — safe to call from any thread ── */
static void read_vram_usage(void) {
    vram_used_mb  = 0;
    vram_total_mb = 0;

    /* Prefer sysfs (AMD / Intel) — zero blocking cost */
    for (int i = 0; i < 4; i++) {
        char used_path[256], total_path[256];
        snprintf(used_path,  sizeof(used_path),
                 "/sys/class/drm/card%d/device/mem_info_vram_used",  i);
        snprintf(total_path, sizeof(total_path),
                 "/sys/class/drm/card%d/device/mem_info_vram_total", i);
        long long used  = read_ll_file_local(used_path);
        long long total = read_ll_file_local(total_path);
        if (used > 0 && total > 0) {
            vram_used_mb  = used  / (1024.0 * 1024.0);
            vram_total_mb = total / (1024.0 * 1024.0);
            return;
        }
    }
    /* NVIDIA: nvidia-smi is expensive — queried asynchronously in
     * heavy_stats_thread() below; skip here to avoid blocking. */
}

static void rrect(cairo_t *cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r,     y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r,     y + r,     r, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}

static gboolean on_draw_ram_bar(GtkWidget *w, cairo_t *cr, gpointer _) {
    int W = gtk_widget_get_allocated_width(w);
    int H = gtk_widget_get_allocated_height(w);
    double pct = ram_total_kb > 0 ? CLAMP((double)ram_used_kb / ram_total_kb, 0.0, 1.0) : 0.0;

    rrect(cr, 0, 0, W, H, H / 2.0);
    cairo_set_source_rgba(cr, 0.13, 0.14, 0.22, 1.0);
    cairo_fill(cr);

    if (pct > 0.01) {
        double fill_w = pct * W;
        cairo_pattern_t *pg = cairo_pattern_create_linear(0, 0, fill_w, 0);
        cairo_pattern_add_color_stop_rgb(pg, 0.0, 0.30, 0.60, 1.00);
        cairo_pattern_add_color_stop_rgb(pg, 0.7, 0.20, 0.50, 0.95);
        if (pct > 0.8)
            cairo_pattern_add_color_stop_rgb(pg, 1.0, 1.0, 0.35, 0.20);
        else
            cairo_pattern_add_color_stop_rgb(pg, 1.0, 0.20, 0.50, 0.95);
        rrect(cr, 0, 0, fill_w, H, H / 2.0);
        cairo_set_source(cr, pg);
        cairo_fill(cr);
        cairo_pattern_destroy(pg);

        cairo_set_source_rgba(cr, 0.30, 0.60, 1.00, 0.20);
        rrect(cr, 0, 0, fill_w, H, H / 2.0);
        cairo_fill(cr);
    }
    return FALSE;
}

static gboolean on_draw_disk_bar(GtkWidget *w, cairo_t *cr, gpointer _) {
    int W = gtk_widget_get_allocated_width(w);
    int H = gtk_widget_get_allocated_height(w);
    double pct = disk_total_gb > 0 ? CLAMP((double)disk_used_gb / disk_total_gb, 0.0, 1.0) : 0.0;

    rrect(cr, 0, 0, W, H, H / 2.0);
    cairo_set_source_rgba(cr, 0.13, 0.14, 0.22, 1.0);
    cairo_fill(cr);

    if (pct > 0.01) {
        double fill_w = pct * W;
        cairo_pattern_t *pg = cairo_pattern_create_linear(0, 0, fill_w, 0);
        cairo_pattern_add_color_stop_rgb(pg, 0.0, 0.75, 0.35, 1.00);
        cairo_pattern_add_color_stop_rgb(pg, 1.0, 0.55, 0.20, 0.90);
        rrect(cr, 0, 0, fill_w, H, H / 2.0);
        cairo_set_source(cr, pg);
        cairo_fill(cr);
        cairo_pattern_destroy(pg);

        cairo_set_source_rgba(cr, 0.75, 0.35, 1.00, 0.20);
        rrect(cr, 0, 0, fill_w, H, H / 2.0);
        cairo_fill(cr);
    }
    return FALSE;
}

static gboolean on_draw_vram_bar(GtkWidget *w, cairo_t *cr, gpointer _) {
    int W = gtk_widget_get_allocated_width(w);
    int H = gtk_widget_get_allocated_height(w);
    double pct = vram_total_mb > 0 ? CLAMP(vram_used_mb / vram_total_mb, 0.0, 1.0) : 0.0;

    rrect(cr, 0, 0, W, H, H / 2.0);
    cairo_set_source_rgba(cr, 0.13, 0.14, 0.22, 1.0);
    cairo_fill(cr);

    if (pct > 0.01) {
        double fill_w = pct * W;
        cairo_pattern_t *pg = cairo_pattern_create_linear(0, 0, fill_w, 0);
        cairo_pattern_add_color_stop_rgb(pg, 0.0, 0.20, 0.75, 0.50);
        cairo_pattern_add_color_stop_rgb(pg, 1.0, 0.92, 0.30, 0.15);
        rrect(cr, 0, 0, fill_w, H, H / 2.0);
        cairo_set_source(cr, pg);
        cairo_fill(cr);
        cairo_pattern_destroy(pg);

        cairo_set_source_rgba(cr, 0.20, 0.75, 0.50, 0.20);
        rrect(cr, 0, 0, fill_w, H, H / 2.0);
        cairo_fill(cr);
    }
    return FALSE;
}

static gboolean update_storage(gpointer _) {
    read_ram_usage();
    read_disk_usage();
    read_vram_usage();

    if (lbl_ram_usage) {
        char used_buf[32], total_buf[32], ram_str[72];
        fmt_bytes(ram_used_kb, used_buf, sizeof(used_buf));
        fmt_bytes(ram_total_kb, total_buf, sizeof(total_buf));
        snprintf(ram_str, sizeof(ram_str), "%s / %s", used_buf, total_buf);
        gtk_label_set_text(GTK_LABEL(lbl_ram_usage), ram_str);
    }

    if (lbl_disk_usage) {
        char disk_str[64];
        snprintf(disk_str, sizeof(disk_str), "%lu GB / %lu GB", disk_used_gb, disk_total_gb);
        gtk_label_set_text(GTK_LABEL(lbl_disk_usage), disk_str);
    }

    if (lbl_vram_usage) {
        char used_buf[32], total_buf[32], vram_str[72];
        fmt_mb(vram_used_mb, used_buf, sizeof(used_buf));
        fmt_mb(vram_total_mb, total_buf, sizeof(total_buf));
        snprintf(vram_str, sizeof(vram_str), "%s / %s", used_buf, total_buf);
        gtk_label_set_text(GTK_LABEL(lbl_vram_usage), vram_str);
    }

    if (ram_bar_draw) gtk_widget_queue_draw(ram_bar_draw);
    if (disk_bar_draw) gtk_widget_queue_draw(disk_bar_draw);
    if (vram_bar_draw) gtk_widget_queue_draw(vram_bar_draw);
    return TRUE;
}

/* ═══════════════════════════════════════════════════════════════════
   SECTION 5 — NETWORK SPARKLINES
   ═══════════════════════════════════════════════════════════════════ */
#define NET_HIST 60

static GtkWidget *lbl_ip    = NULL;
static GtkWidget *net_draw  = NULL;
static GtkWidget *lbl_ul_speed = NULL;
static GtkWidget *lbl_dl_speed = NULL;
static double  ul_hist[NET_HIST], dl_hist[NET_HIST];
static int     net_idx    = 0;
static guint64 last_rx=0, last_tx=0;
static gboolean net_first = TRUE;

static void fmt_net_rate(double kbps, char *out, size_t len) {
    if (kbps >= 1024.0 * 1024.0)
        snprintf(out, len, "%.1f GB/s", kbps / (1024.0 * 1024.0));
    else if (kbps >= 1024.0)
        snprintf(out, len, "%.1f MB/s", kbps / 1024.0);
    else
        snprintf(out, len, "%.0f KB/s", kbps);
}

static gboolean read_net(guint64 *rx, guint64 *tx) {
    FILE *f = fopen("/proc/net/dev","r");
    if (!f) return FALSE;
    char line[256];
    guint64 tr = 0, tt = 0;

    if (!fgets(line, sizeof(line), f) || !fgets(line, sizeof(line), f)) {
        fclose(f);
        return FALSE;
    }

    while (fgets(line,sizeof(line),f)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        char iface[32];
        if (sscanf(line, " %31[^:]", iface) != 1) continue;
        if (strcmp(iface,"lo")==0) continue;
        guint64 r = 0, t = 0;
        if (sscanf(colon + 1, "%" G_GUINT64_FORMAT " %*u %*u %*u %*u %*u %*u %*u %" G_GUINT64_FORMAT,
                   &r, &t) == 2) {
            tr += r;
            tt += t;
        }
    }
    fclose(f);
    *rx = tr;
    *tx = tt;
    return TRUE;
}

static gboolean sample_net(gpointer _) {
    guint64 rx,tx;
    if (!read_net(&rx,&tx)) return TRUE;
    if (!net_first) {
        double dl=(double)(rx-last_rx)/1024.0;
        double ul=(double)(tx-last_tx)/1024.0;
        dl_hist[net_idx]=dl>0?dl:0;
        ul_hist[net_idx]=ul>0?ul:0;
        net_idx=(net_idx+1)%NET_HIST;
        if (lbl_ul_speed) {
            char buf[32];
            fmt_net_rate(ul_hist[(net_idx + NET_HIST - 1) % NET_HIST], buf, sizeof(buf));
            gtk_label_set_text(GTK_LABEL(lbl_ul_speed), buf);
        }
        if (lbl_dl_speed) {
            char buf[32];
            fmt_net_rate(dl_hist[(net_idx + NET_HIST - 1) % NET_HIST], buf, sizeof(buf));
            gtk_label_set_text(GTK_LABEL(lbl_dl_speed), buf);
        }
        if (net_draw) gtk_widget_queue_draw(net_draw);
    }
    net_first=FALSE; last_rx=rx; last_tx=tx; return TRUE;
}

static void draw_spark(cairo_t *cr, double *hist, int n, int cur,
                       double r,double g,double b,
                       double x0,double y0,double w,double h) {
    double mx=1.0;
    for(int i=0;i<n;i++) if(hist[i]>mx) mx=hist[i];
    cairo_set_line_width(cr,1.5);
    cairo_move_to(cr,x0,y0+h);
    for(int i=0;i<n;i++){
        int idx=(cur+i)%n;
        double px=x0+(double)i/(n-1)*w;
        double py=y0+h-(hist[idx]/mx)*h;
        i==0?cairo_line_to(cr,px,py):cairo_line_to(cr,px,py);
    }
    cairo_line_to(cr,x0+w,y0+h); cairo_close_path(cr);
    cairo_set_source_rgba(cr,r,g,b,0.15); cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr,r,g,b,0.85); cairo_set_line_width(cr,1.5); cairo_stroke(cr);
}

static gboolean on_net_draw(GtkWidget *w, cairo_t *cr, gpointer _) {
    int W=gtk_widget_get_allocated_width(w);
    int H=gtk_widget_get_allocated_height(w);
    cairo_set_source_rgba(cr,0,0,0,0); cairo_paint(cr);
    int half=H/2-2;
    draw_spark(cr,ul_hist,NET_HIST,net_idx, 0.95,0.47,0.66, 2,2,W-4,half);
    draw_spark(cr,dl_hist,NET_HIST,net_idx, 0.54,0.71,0.98, 2,half+6,W-4,half);
    return FALSE;
}

static char* get_ip(void) {
    /* Read /proc/net/fib_trie or fall back to run_cmd.
     * We keep run_cmd here because get_ip() is called once at build time
     * and already deferred to refresh_sysinfo (60s timer) after that.
     * The initial call is in create_sidebar_content() which now runs
     * after the window is shown — acceptable one-time cost. */
    return run_cmd(
        "ip -o -f inet addr show scope global 2>/dev/null | awk '{print $4}' | head -1");
}

/* ═══════════════════════════════════════════════════════════════════
   SECTION 6 — SYSTEM MONITOR (ring gauges)
   ═══════════════════════════════════════════════════════════════════ */
static GtkWidget *gauge_draw = NULL;

typedef struct {
    double val;
    char name[16];
    char label[24];
    double r,g,b;
} Ring;
static Ring rings[4] = {
    {0,"CPU",  "CPU%", 0.85,0.87,0.25},
    {0,"TEMP", "39°C", 0.96,0.60,0.42},
    {0,"GPU",  "GPU%", 0.38,0.72,0.62},
    {0,"RAM",  "RAM%", 0.80,0.55,0.98},
};

static guint64 cpu_prev_idle=0, cpu_prev_total=0;

static double get_cpu_pct(void) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0;
    unsigned long long u, n, s, i, iow, irq, sirq, steal;
    if (fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
               &u, &n, &s, &i, &iow, &irq, &sirq, &steal) != 8) {
        fclose(f);
        return 0;
    }
    fclose(f);
    guint64 idle = i + iow, total = u + n + s + i + iow + irq + sirq + steal;
    double pct = 0;
    if (cpu_prev_total > 0 && total > cpu_prev_total)
        pct = 1.0 - (double)(idle - cpu_prev_idle) / (double)(total - cpu_prev_total);
    cpu_prev_idle = idle;
    cpu_prev_total = total;
    return pct<0?0:(pct>1?1:pct);
}
static double get_ram_pct(void) {
    FILE *f=fopen("/proc/meminfo","r"); if(!f) return 0;
    long tot=0,avail=0; char k[64]; long v;
    while(fscanf(f,"%63s %ld %*s",k,&v)==2){
        if(!strcmp(k,"MemTotal:"))     tot=v;
        if(!strcmp(k,"MemAvailable:")) avail=v;
    }
    fclose(f); return tot>0?(double)(tot-avail)/tot:0;
}
/* ─── CPU temp: read sysfs directly, no popen ──────────────────────── */
static int read_cputemp_sysfs(void) {
    int best = 0;
    for (int i = 0; i < 16; i++) {
        char path[128];
        snprintf(path, sizeof(path),
                 "/sys/class/thermal/thermal_zone%d/temp", i);
        FILE *f = fopen(path, "r");
        if (!f) break;
        int t = 0;
        if (fscanf(f, "%d", &t) == 1 && t > best) best = t;
        fclose(f);
    }
    return best / 1000; /* millidegrees → degrees */
}

static int read_int_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int v = -1;
    if (fscanf(f, "%d", &v) != 1) v = -1;
    fclose(f);
    return v;
}

/* ─── Phase 2: Heavy async stats (nvidia-smi) via GTask ─────────────── */

typedef struct {
    /* GPU (nvidia-smi or sysfs) */
    double gpu_usage;   /* 0..1 */
    double gpu_label;   /* percent integer */
    double vram_used;   /* MB */
    double vram_total;  /* MB */
    /* CPU temp */
    int    cpu_temp;    /* °C */
} HeavyStatsResult;

static gboolean s_heavy_running = FALSE; /* prevent overlapping tasks */

/* Runs in a worker thread — NO GTK calls allowed here */
static void heavy_stats_thread(GTask        *task,
                               gpointer      source,
                               gpointer      task_data,
                               GCancellable *cancellable) {
    (void)source; (void)task_data; (void)cancellable;
    HeavyStatsResult *res = g_new0(HeavyStatsResult, 1);

    /* --- nvidia-smi: GPU usage + VRAM (one call for both) --- */
    FILE *f = popen("nvidia-smi --query-gpu=utilization.gpu,memory.used,memory.total"
                    " --format=csv,noheader,nounits 2>/dev/null", "r");
    if (f) {
        float gpu = 0, vmused = 0, vmtotal = 0;
        char line[256] = {0};
        if (fgets(line, sizeof(line), f)) {
            /* nvidia-smi output: "gpu%, vmused MB, vmtotal MB" */
            for (char *p = line; *p; p++) if (*p == ',') *p = ' ';
            if (sscanf(line, "%f %f %f", &gpu, &vmused, &vmtotal) == 3) {
                res->gpu_usage  = CLAMP(gpu  / 100.0, 0.0, 1.0);
                res->gpu_label  = gpu;
                res->vram_used  = vmused;
                res->vram_total = vmtotal;
            }
        }
        pclose(f);
    }

    /* --- sysfs GPU fallback (AMD/Intel) if nvidia-smi gave nothing --- */
    if (res->vram_total <= 0) {
        for (int i = 0; i < 4; i++) {
            char p1[256], p2[256];
            snprintf(p1, sizeof(p1), "/sys/class/drm/card%d/device/mem_info_vram_used",  i);
            snprintf(p2, sizeof(p2), "/sys/class/drm/card%d/device/mem_info_vram_total", i);
            long long u = read_ll_file_local(p1);
            long long t = read_ll_file_local(p2);
            if (u > 0 && t > 0) {
                res->vram_used  = u / (1024.0 * 1024.0);
                res->vram_total = t / (1024.0 * 1024.0);
                break;
            }
        }
    }
    if (res->gpu_usage <= 0) {
        for (int i = 0; i < 4; i++) {
            char p[256];
            snprintf(p, sizeof(p), "/sys/class/drm/card%d/device/gpu_busy_percent", i);
            int load = read_int_file(p);
            if (load >= 0) {
                res->gpu_usage = CLAMP(load / 100.0, 0.0, 1.0);
                res->gpu_label = load;
                break;
            }
        }
    }

    /* --- CPU temp via sysfs (fast, no popen) --- */
    res->cpu_temp = read_cputemp_sysfs();

    g_task_return_pointer(task, res, g_free);
}

/* Runs on the GTK main thread after the worker finishes */
static void on_heavy_stats_done(GObject      *src,
                                GAsyncResult *result,
                                gpointer      user_data) {
    (void)src; (void)user_data;
    s_heavy_running = FALSE;

    HeavyStatsResult *res = g_task_propagate_pointer(G_TASK(result), NULL);
    if (!res) return;

    /* Update gauge ring values */
    rings[2].val = res->gpu_usage;
    snprintf(rings[2].label, sizeof(rings[2].label), "%.0f%%", res->gpu_label);

    rings[1].val = res->cpu_temp > 0 ? res->cpu_temp / 110.0 : 0;
    snprintf(rings[1].label, sizeof(rings[1].label), "%d" "\xc2\xb0" "C", res->cpu_temp);

    /* Update VRAM bars */
    vram_used_mb  = res->vram_used;
    vram_total_mb = res->vram_total;
    if (lbl_vram_usage) {
        char u[32], t[32], buf[72];
        fmt_mb(vram_used_mb,  u, sizeof(u));
        fmt_mb(vram_total_mb, t, sizeof(t));
        snprintf(buf, sizeof(buf), "%s / %s", u, t);
        gtk_label_set_text(GTK_LABEL(lbl_vram_usage), buf);
    }
    if (vram_bar_draw) gtk_widget_queue_draw(vram_bar_draw);
    if (gauge_draw)    gtk_widget_queue_draw(gauge_draw);

    g_free(res);
}

/* Timer callback: dispatch async task if none is running */
static gboolean update_gauges(gpointer user_data) {
    (void)user_data;

    /* Light, synchronous stats — CPU% and RAM% are trivially fast */
    double cpu = get_cpu_pct();
    double ram = get_ram_pct();
    rings[0].val = cpu;
    snprintf(rings[0].label, sizeof(rings[0].label), "%.0f%%", cpu * 100);
    rings[3].val = ram;
    snprintf(rings[3].label, sizeof(rings[3].label), "%.0f%%", ram * 100);
    if (gauge_draw) gtk_widget_queue_draw(gauge_draw);

    /* Heavy stats (nvidia-smi, sysfs GPU/VRAM) — run in background thread */
    if (!s_heavy_running) {
        s_heavy_running = TRUE;
        GTask *task = g_task_new(NULL, NULL, on_heavy_stats_done, NULL);
        g_task_run_in_thread(task, heavy_stats_thread);
        g_object_unref(task);
    }

    return G_SOURCE_CONTINUE;
}

#define RING_RADIUS  26.0
#define RING_WIDTH    6.0
#define ANG_START    (M_PI*0.75)
#define ANG_SWEEP    (M_PI*1.5)

static void draw_one_ring(cairo_t *cr, double cx, double cy,
                          double val, double r, double g, double b,
                          const char *name, const char *lbl) {
    /* track */
    cairo_set_source_rgba(cr,1,1,1,0.07);
    cairo_set_line_width(cr,RING_WIDTH);
    cairo_arc(cr,cx,cy,RING_RADIUS,ANG_START,ANG_START+ANG_SWEEP); cairo_stroke(cr);
    /* fill */
    if(val>0.001){
        cairo_set_source_rgba(cr,r,g,b,0.9);
        cairo_arc(cr,cx,cy,RING_RADIUS,ANG_START,ANG_START+ANG_SWEEP*val); cairo_stroke(cr);
        /* glow dot */
        double ea=ANG_START+ANG_SWEEP*val;
        cairo_set_source_rgba(cr,r,g,b,0.55);
        cairo_arc(cr,cx+RING_RADIUS*cos(ea),cy+RING_RADIUS*sin(ea),
                  RING_WIDTH/2.0+1.5,0,2*M_PI); cairo_fill(cr);
    }
    /* title inside ring */
    cairo_set_source_rgba(cr,0.62,0.66,0.78,0.92);
    cairo_select_font_face(cr,"Sans",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr,8.0);
    cairo_text_extents_t te_name; cairo_text_extents(cr,name,&te_name);
    cairo_move_to(cr,cx-te_name.width/2-te_name.x_bearing, cy-te_name.height/2-te_name.y_bearing);
    cairo_show_text(cr,name);

    /* value */
    cairo_set_source_rgba(cr,0.8,0.82,0.88,0.95);
    cairo_select_font_face(cr,"Sans",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr,9.0);
    cairo_text_extents_t te; cairo_text_extents(cr,lbl,&te);
    cairo_move_to(cr,cx-te.width/2-te.x_bearing, cy+RING_RADIUS+11);
    cairo_show_text(cr,lbl);
}

static gboolean on_gauge_draw(GtkWidget *w, cairo_t *cr, gpointer _) {
    int W = gtk_widget_get_allocated_width(w);
    int H = gtk_widget_get_allocated_height(w);
    cairo_set_source_rgba(cr,0,0,0,0); cairo_paint(cr);
    /* 4 rings in a single horizontal row, vertically centred */
    double step = W / 4.0;
    double cy   = H * 0.48;
    for(int i = 0; i < 4; i++)
        draw_one_ring(cr, step*i + step*0.5, cy,
                      rings[i].val,
                      rings[i].r, rings[i].g, rings[i].b,
                      rings[i].name, rings[i].label);
    return FALSE;
}

/* ═══════════════════════════════════════════════════════════════════
   GLOBAL CSS
   ═══════════════════════════════════════════════════════════════════ */
static const char *SIDEBAR_CSS =
    /* ── outer window: no padding, clips children ── */
    "#sidebar {"
    "  border-radius: 16px;"
    "  padding: 0;"
    "  background-color: transparent;"
    "}"

    /* ══ TOP ROW: clock panel (left) ══ */
    "#clock_panel {"
    "  padding: 16px;"
    "  border-right: 1px solid rgba(255,255,255,0.06);"
    "}"
    "#lbl_time {"
    "  color: #ffffff;"
    "  font-weight: 700;"
    "  font-size: 42px;"
    "  letter-spacing: -1px;"
    "}"
    "#lbl_date {"
    "  color: rgba(255,255,255,0.5);"
    "  font-size: 13px;"
    "  font-weight: 400;"
    "  margin-top: 4px;"
    "}"

    /* ══ TOP ROW: calendar panel (right) ══ */
    "#cal_panel {"
    "  padding: 14px;"
    "}"
    "#cal_nav_lbl {"
    "  color: rgba(255,255,255,0.85);"
    "  font-size: 12px;"
    "  font-weight: 700;"
    "}"
    "#cal_btn {"
    "  background: none; border: none;"
    "  color: rgba(255,255,255,0.4); font-size: 11px;"
    "  padding: 0 2px; min-width:0; min-height:0;"
    "}"
    "#cal_btn:hover { color: #cba6f7; }"
    "#cal_hdr  { color: rgba(255,255,255,0.3); font-size: 10px; font-weight: 700; }"
    "#cal_day  { color: rgba(255,255,255,0.6); font-size: 10px; }"
    "#cal_fade { color: rgba(255,255,255,0.2); font-size: 10px; }"
    "#cal_today {"
    "  color: #ffffff; background-color: #7f77dd;"
    "  border-radius: 50%; font-weight: 700; font-size: 10px;"
    "}"

    /* ══ INFO ROW: 4 cells ══ */
    "#info_row { }"
    "#info_cell {"
    "  padding: 10px 14px;"
    "}"
    "#si_icon  { color: #89dceb; font-size: 10px; }"
    "#si_value { color: rgba(255,255,255,0.7); font-size: 11px; font-weight: 400; }"

    /* ══ STORAGE SECTION ══ */
    "#storage_box {"
    "  padding: 14px 16px 10px 16px;"
    "  border-top: 1px solid rgba(255,255,255,0.06);"
    "}"
    "#sec_title {"
    "  color: rgba(255,255,255,0.35);"
    "  font-size: 10px;"
    "  font-weight: 700;"
    "  letter-spacing: 1px;"
    "  margin-bottom: 8px;"
    "}"
    "#stat_hdr {"
    "  color: rgba(255,255,255,0.5);"
    "  font-family: monospace;"
    "  font-size: 10px;"
    "}"
    "#stat_val {"
    "  color: rgba(255,255,255,0.4);"
    "  font-family: monospace;"
    "  font-size: 10px;"
    "}"

    /* ══ NETWORK SECTION ══ */
    "#net_box {"
    "  padding: 12px 16px;"
    "  border-top: 1px solid rgba(255,255,255,0.06);"
    "}"
    "#net_ip  { color: rgba(255,255,255,0.6); font-size: 12px; }"
    "#net_up  { color: #f38ba8; font-size: 11px; }"
    "#net_down{ color: #89b4fa; font-size: 11px; }"

    /* ══ GAUGES ROW ══ */
    "#gauge_row {"
    "  border-top: 1px solid rgba(255,255,255,0.06);"
    "  margin-top: 10px;"
    "  margin-bottom: 10px;"
    "}"
    "#gauge_cell {"
    "  padding: 12px 8px;"
    "}"
    "#gauge_val   { color: #ffffff; font-size: 13px; font-weight: 500; }"
    "#gauge_label { color: rgba(255,255,255,0.4); font-size: 10px; }"

    /* divider unused but keep for compat */
    "#divider { margin: 0; }";

/* ═══════════════════════════════════════════════════════════════════
   POPUP SUPPORT
   ═══════════════════════════════════════════════════════════════════ */
static GtkCssProvider *bg_css = NULL;
static gboolean sidebar_css_loaded = FALSE;

#define SIDEBAR_POPUP_WIDTH 500
#define SIDEBAR_POPUP_PAD 8

static void set_theme(const char *hex_color, double opacity) {
    (void)hex_color;
    (void)opacity;
    if (!bg_css) return;
    gtk_css_provider_load_from_data(bg_css, "#sidebar { background-color: transparent; }", -1, NULL);
}

/* ═══════════════════════════════════════════════════════════════════
   HELPER: thin horizontal separator
   ═══════════════════════════════════════════════════════════════════ */
/* ═══════════════════════════════════════════════════════════════════
   POPUP CONTENT FACTORY
   ═══════════════════════════════════════════════════════════════════ */
static GtkWidget* create_sidebar_content(void) {
    memset(ul_hist,0,sizeof(ul_hist));
    memset(dl_hist,0,sizeof(dl_hist));

    /* ── init calendar view to current month ── */
    time_t now0=time(NULL); struct tm *t0=localtime(&now0);
    view_year=t0->tm_year+1900; view_month=t0->tm_mon;

    /* ── CSS ── */
    if (!sidebar_css_loaded) {
        GtkCssProvider *css = gtk_css_provider_new();
        gtk_css_provider_load_from_data(css, SIDEBAR_CSS, -1, NULL);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css);
        sidebar_css_loaded = TRUE;
    }

    /*
     * LAYOUT — matches HTML mockup exactly (no titlebar):
     *
     *  ┌─────────────────────────────────────────┐
     *  │  [clock panel]  │  [calendar panel]     │  ← top_row (GtkGrid 1×2)
     *  ├────────┬────────┬────────┬──────────────┤
     *  │ comp   │ shell  │ uptime │ packages      │  ← info_row (GtkGrid 1×4)
     *  ├─────────────────────────────────────────┤
     *  │  Storage: RAM / DISK / VRAM bars         │  ← storage_box (VBox)
     *  ├─────────────────────────────────────────┤
     *  │  IP + speeds          [sparkline]        │  ← net_box (HBox)
     *  ├──────────┬──────────┬──────────┬────────┤
     *  │  CPU     │  TEMP    │  GPU     │  RAM%  │  ← gauge_row (GtkGrid 1×4)
     *  └──────────┴──────────┴──────────┴────────┘
     */

    /* ── root card ── */
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(card, "sidebar");
    gtk_widget_set_size_request(card, SIDEBAR_POPUP_WIDTH, -1);

    GtkStyleContext *context = gtk_widget_get_style_context(card);
    bg_css = gtk_css_provider_new();
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(bg_css), 801);
    set_theme("#0000003f", 1.0);

    /* ══════════════════════════════════════════
       ROW 1 — top_row: clock (left) | calendar (right)
       ══════════════════════════════════════════ */
    GtkWidget *top_row = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(top_row), TRUE);
    gtk_box_pack_start(GTK_BOX(card), top_row, FALSE, FALSE, 0);

    /* ── clock panel (left cell) ── */
    GtkWidget *clock_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_name(clock_panel, "clock_panel");
    gtk_widget_set_valign(clock_panel, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(clock_panel, GTK_ALIGN_FILL);

    lbl_time = gtk_label_new("00:00");
    gtk_widget_set_name(lbl_time, "lbl_time");
    gtk_label_set_xalign(GTK_LABEL(lbl_time), 0.5);
    gtk_box_pack_start(GTK_BOX(clock_panel), lbl_time, FALSE, FALSE, 0);

    lbl_date = gtk_label_new("");
    gtk_widget_set_name(lbl_date, "lbl_date");
    gtk_label_set_xalign(GTK_LABEL(lbl_date), 0.5);
    gtk_box_pack_start(GTK_BOX(clock_panel), lbl_date, FALSE, FALSE, 0);

    gtk_grid_attach(GTK_GRID(top_row), clock_panel, 0, 0, 1, 1);

    /* ── calendar panel (right cell) ── */
    GtkWidget *cal_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_name(cal_panel, "cal_panel");

    /* single nav row: "< April 2026 >" */
    GtkWidget *nav_m = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_halign(nav_m, GTK_ALIGN_CENTER);
    GtkWidget *bpm = gtk_button_new_with_label("<"); gtk_widget_set_name(bpm, "cal_btn");
    g_signal_connect(bpm, "clicked", G_CALLBACK(on_prev_month), NULL);
    lbl_month = gtk_label_new(""); gtk_widget_set_name(lbl_month, "cal_nav_lbl");
    GtkWidget *bnm = gtk_button_new_with_label(">"); gtk_widget_set_name(bnm, "cal_btn");
    g_signal_connect(bnm, "clicked", G_CALLBACK(on_next_month), NULL);
    gtk_box_pack_start(GTK_BOX(nav_m), bpm, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(nav_m), lbl_month, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(nav_m), bnm, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(cal_panel), nav_m, FALSE, FALSE, 0);

    /* year nav */
    GtkWidget *nav_y = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_halign(nav_y, GTK_ALIGN_CENTER);
    GtkWidget *bpy = gtk_button_new_with_label("<"); gtk_widget_set_name(bpy, "cal_btn");
    g_signal_connect(bpy, "clicked", G_CALLBACK(on_prev_year), NULL);
    lbl_year = gtk_label_new(""); gtk_widget_set_name(lbl_year, "cal_nav_lbl");
    GtkWidget *bny = gtk_button_new_with_label(">"); gtk_widget_set_name(bny, "cal_btn");
    g_signal_connect(bny, "clicked", G_CALLBACK(on_next_year), NULL);
    gtk_box_pack_start(GTK_BOX(nav_y), bpy, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(nav_y), lbl_year, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(nav_y), bny, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(cal_panel), nav_y, FALSE, FALSE, 0);

    cal_grid = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(cal_grid), TRUE);
    gtk_grid_set_row_spacing(GTK_GRID(cal_grid), 1);
    gtk_widget_set_halign(cal_grid, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(cal_panel), cal_grid, FALSE, FALSE, 4);

    gtk_grid_attach(GTK_GRID(top_row), cal_panel, 1, 0, 1, 1);

    /* ══════════════════════════════════════════
       ROW 2 — info_row: 4 equal cells (GtkGrid)
       ══════════════════════════════════════════ */
    GtkWidget *info_row = gtk_grid_new();
    gtk_widget_set_name(info_row, "info_row");
    gtk_grid_set_column_homogeneous(GTK_GRID(info_row), TRUE);
    gtk_box_pack_start(GTK_BOX(card), info_row, FALSE, FALSE, 0);

    #define INFO_CELL(col, icon, lbl_ptr) { \
        GtkWidget *cell = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4); \
        gtk_widget_set_name(cell, "info_cell"); \
        GtkWidget *ic = gtk_label_new(icon); \
        gtk_widget_set_name(ic, "si_icon"); \
        lbl_ptr = gtk_label_new("..."); \
        gtk_widget_set_name(lbl_ptr, "si_value"); \
        gtk_label_set_xalign(GTK_LABEL(lbl_ptr), 0.0); \
        gtk_label_set_ellipsize(GTK_LABEL(lbl_ptr), PANGO_ELLIPSIZE_END); \
        gtk_box_pack_start(GTK_BOX(cell), ic, FALSE, FALSE, 0); \
        gtk_box_pack_start(GTK_BOX(cell), lbl_ptr, TRUE, TRUE, 0); \
        gtk_grid_attach(GTK_GRID(info_row), cell, col, 0, 1, 1); \
    }
    INFO_CELL(0, "DE", lbl_compositor)
    INFO_CELL(1, "Shell", lbl_shell)
    INFO_CELL(2, "Uptime", lbl_uptime)
    INFO_CELL(3, "Packages", lbl_packages)
    #undef INFO_CELL

    { char *c=get_compositor(),*s=get_shell(),*u=get_uptime_str(),*p=get_packages();
      gtk_label_set_text(GTK_LABEL(lbl_compositor), c);
      gtk_label_set_text(GTK_LABEL(lbl_shell), s);
      gtk_label_set_text(GTK_LABEL(lbl_uptime), u);
      gtk_label_set_text(GTK_LABEL(lbl_packages), p);
      g_free(c); g_free(s); g_free(u); g_free(p); }

    /* ══════════════════════════════════════════
       ROW 3 — storage_box: STORAGE (title) + 3 bars
       ══════════════════════════════════════════ */
    GtkWidget *storage_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_name(storage_box, "storage_box");
    gtk_box_pack_start(GTK_BOX(card), storage_box, FALSE, FALSE, 0);

    GtkWidget *storage_title = gtk_label_new("STORAGE");
    gtk_widget_set_name(storage_title, "sec_title");
    gtk_label_set_xalign(GTK_LABEL(storage_title), 0.0);
    gtk_box_pack_start(GTK_BOX(storage_box), storage_title, FALSE, FALSE, 0);

    /* helper macro: one bar row = label | bar_draw | value */
    #define BAR_ROW(parent, hdr_text, hdr_id, lbl_ptr, bar_ptr, cb) { \
        GtkWidget *brow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8); \
        GtkWidget *hdr = gtk_label_new(hdr_text); \
        gtk_widget_set_name(hdr, hdr_id); \
        gtk_widget_set_size_request(hdr, 42, -1); \
        gtk_label_set_xalign(GTK_LABEL(hdr), 0.0); \
        bar_ptr = gtk_drawing_area_new(); \
        gtk_widget_set_size_request(bar_ptr, -1, 5); \
        g_signal_connect(bar_ptr, "draw", G_CALLBACK(cb), NULL); \
        lbl_ptr = gtk_label_new("-- / --"); \
        gtk_widget_set_name(lbl_ptr, "stat_val"); \
        gtk_widget_set_size_request(lbl_ptr, 90, -1); \
        gtk_label_set_xalign(GTK_LABEL(lbl_ptr), 1.0); \
        gtk_box_pack_start(GTK_BOX(brow), hdr, FALSE, FALSE, 0); \
        gtk_box_pack_start(GTK_BOX(brow), bar_ptr, TRUE, TRUE, 0); \
        gtk_box_pack_start(GTK_BOX(brow), lbl_ptr, FALSE, FALSE, 0); \
        gtk_box_pack_start(GTK_BOX(parent), brow, FALSE, FALSE, 0); \
    }
    BAR_ROW(storage_box, "RAM",    "stat_hdr", lbl_ram_usage,  ram_bar_draw,  on_draw_ram_bar)
    BAR_ROW(storage_box, "DISK /", "stat_hdr", lbl_disk_usage, disk_bar_draw, on_draw_disk_bar)
    BAR_ROW(storage_box, "VRAM",   "stat_hdr", lbl_vram_usage, vram_bar_draw, on_draw_vram_bar)
    #undef BAR_ROW

    /* ══════════════════════════════════════════
       ROW 4 — net_box: IP+speeds (left) | sparkline (right)
       ══════════════════════════════════════════ */
    GtkWidget *net_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_name(net_box, "net_box");
    gtk_box_pack_start(GTK_BOX(card), net_box, FALSE, FALSE, 0);

    /* left: IP + up/down speeds */
    GtkWidget *net_left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(net_box), net_left, FALSE, FALSE, 0);

    lbl_ip = gtk_label_new("...");
    gtk_widget_set_name(lbl_ip, "net_ip");
    gtk_label_set_xalign(GTK_LABEL(lbl_ip), 0.0);
    gtk_box_pack_start(GTK_BOX(net_left), lbl_ip, FALSE, FALSE, 0);
    { char *ip = get_ip(); gtk_label_set_text(GTK_LABEL(lbl_ip), ip); g_free(ip); }

    GtkWidget *speed_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *ul_lbl = gtk_label_new("↑"); gtk_widget_set_name(ul_lbl, "net_up");
    lbl_ul_speed = gtk_label_new("0 KB/s"); gtk_widget_set_name(lbl_ul_speed, "net_up");
    GtkWidget *dl_lbl = gtk_label_new("↓"); gtk_widget_set_name(dl_lbl, "net_down");
    lbl_dl_speed = gtk_label_new("0 KB/s"); gtk_widget_set_name(lbl_dl_speed, "net_down");
    gtk_box_pack_start(GTK_BOX(speed_row), ul_lbl,      FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(speed_row), lbl_ul_speed,FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(speed_row), dl_lbl,      FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(speed_row), lbl_dl_speed,FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(net_left), speed_row, FALSE, FALSE, 0);

    /* right: sparkline drawing area */
    net_draw = gtk_drawing_area_new();
    gtk_widget_set_hexpand(net_draw, TRUE);
    gtk_widget_set_halign(net_draw, GTK_ALIGN_FILL);
    gtk_widget_set_size_request(net_draw, 240, 80);
    g_signal_connect(net_draw, "draw", G_CALLBACK(on_net_draw), NULL);
    gtk_box_pack_end(GTK_BOX(net_box), net_draw, TRUE, TRUE, 0);

    /* ══════════════════════════════════════════
       ROW 5 — gauge_row: 4 equal cells (GtkGrid)
       ══════════════════════════════════════════ */
    GtkWidget *gauge_row = gtk_grid_new();
    gtk_widget_set_name(gauge_row, "gauge_row");
    gtk_grid_set_column_homogeneous(GTK_GRID(gauge_row), TRUE);
    gtk_box_pack_start(GTK_BOX(card), gauge_row, FALSE, FALSE, 0);

    /* Each gauge cell gets its own drawing_area (120px tall = ring + label) */
    gauge_draw = gtk_drawing_area_new();
    gtk_widget_set_size_request(gauge_draw, -1, 120);
    g_signal_connect(gauge_draw, "draw", G_CALLBACK(on_gauge_draw), NULL);
    gtk_grid_attach(GTK_GRID(gauge_row), gauge_draw, 0, 0, 4, 1);

    /* ─────────── TIMERS ─────────── */
    update_clock(NULL);
    rebuild_calendar();
    update_storage(NULL);
    /* update_gauges: first async run deferred 600ms so the window
     * can render before nvidia-smi starts — avoids startup freeze. */
    g_timeout_add(600, update_gauges, NULL);

    /* Persistent timers stored in sidebar_timers_start/stop below */
    gtk_widget_show_all(card);
    return card;
}

/* ═══════════════════════════════════════════════════════════════════
   POPUP WINDOW
   ═══════════════════════════════════════════════════════════════════ */
static void ensure_rgba_visual(GtkWidget *widget) {
    GdkScreen *screen;
    GdkVisual *visual;

    if (!widget) return;

    screen = gtk_widget_get_screen(widget);
    if (!screen) return;

    visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(widget, visual);
        gtk_widget_set_app_paintable(widget, TRUE);
    }
}

static gboolean draw_sidebar_popup_clear(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)widget;
    (void)user_data;

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);
    return FALSE;
}

static gboolean draw_sidebar_popup_background(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    GtkAllocation alloc;
    const double radius = 16.0;

    (void)user_data;
    gtk_widget_get_allocation(widget, &alloc);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.392);
    cairo_new_sub_path(cr);
    cairo_arc(cr, radius, radius, radius, G_PI, 3.0 * G_PI / 2.0);
    cairo_arc(cr, alloc.width - radius, radius, radius, 3.0 * G_PI / 2.0, 0.0);
    cairo_arc(cr, alloc.width - radius, alloc.height - radius, radius, 0.0, G_PI / 2.0);
    cairo_arc(cr, radius, alloc.height - radius, radius, G_PI / 2.0, G_PI);
    cairo_close_path(cr);
    cairo_fill_preserve(cr);

    cairo_set_source_rgba(cr, 0.133, 0.133, 0.133, 1.0);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    return FALSE;
}

static gboolean get_monitor_geometry_for_widget(GtkWidget *widget, GdkRectangle *geom) {
    GdkDisplay *display;
    GdkMonitor *monitor = NULL;
    GdkWindow *window;

    if (!widget || !geom) return FALSE;

    display = gtk_widget_get_display(widget);
    if (!display) return FALSE;

    window = gtk_widget_get_window(widget);
    if (window) {
        monitor = gdk_display_get_monitor_at_window(display, window);
    }
    if (!monitor) {
        monitor = gdk_display_get_primary_monitor(display);
    }
    if (!monitor && gdk_display_get_n_monitors(display) > 0) {
        monitor = gdk_display_get_monitor(display, 0);
    }
    if (!monitor) return FALSE;

    gdk_monitor_get_geometry(monitor, geom);
    return TRUE;
}

static void reposition_sidebar_popup(GtkWidget *popup, GtkWidget *relative_to) {
    GtkAllocation alloc;
    GdkRectangle monitor = {0};
    GdkWindow *window;
    gint origin_x = 0;
    gint popup_x;
    gint popup_y;

    if (!popup || !relative_to) return;
    if (!gtk_widget_get_realized(relative_to)) return;
    if (!get_monitor_geometry_for_widget(relative_to, &monitor)) return;

    window = gtk_widget_get_window(relative_to);
    if (!window) return;

    gtk_widget_get_allocation(relative_to, &alloc);
    gdk_window_get_origin(window, &origin_x, NULL);

    popup_x = origin_x + alloc.x + (alloc.width / 2) - (SIDEBAR_POPUP_WIDTH / 2);
    popup_x = CLAMP(popup_x,
                    monitor.x + SIDEBAR_POPUP_PAD,
                    monitor.x + monitor.width - SIDEBAR_POPUP_WIDTH - SIDEBAR_POPUP_PAD);
    popup_y = monitor.y;

    panel_window_backend_set_anchor(GTK_WINDOW(popup), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    panel_window_backend_set_anchor(GTK_WINDOW(popup), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    panel_window_backend_set_anchor(GTK_WINDOW(popup), GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);
    panel_window_backend_set_anchor(GTK_WINDOW(popup), GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);
    panel_window_backend_set_margin(GTK_WINDOW(popup), GTK_LAYER_SHELL_EDGE_TOP, popup_y - monitor.y);
    panel_window_backend_set_margin(GTK_WINDOW(popup), GTK_LAYER_SHELL_EDGE_LEFT, popup_x - monitor.x);
}

/* ─── Phase 3: timer IDs for start/stop on show/hide ────────────────── */
static guint s_timer_clock   = 0;
static guint s_timer_storage = 0;
static guint s_timer_gauges  = 0;
static guint s_timer_net     = 0;
static guint s_timer_sysinfo = 0;

static void sidebar_timers_start(void) {
    if (!s_timer_clock)   s_timer_clock   = g_timeout_add(1000,  update_clock,    NULL);
    if (!s_timer_storage) s_timer_storage = g_timeout_add(2000,  update_storage,  NULL);
    if (!s_timer_gauges)  s_timer_gauges  = g_timeout_add(2000,  update_gauges,   NULL);
    if (!s_timer_net)     s_timer_net     = g_timeout_add(1000,  sample_net,      NULL);
    if (!s_timer_sysinfo) s_timer_sysinfo = g_timeout_add(60000, refresh_sysinfo, NULL);
}

static void sidebar_timers_stop(void) {
    /* Stop only the GPU-heavy timers; lightweight timers keep running. */
    if (s_timer_gauges) { g_source_remove(s_timer_gauges); s_timer_gauges = 0; }
    if (s_timer_net)    { g_source_remove(s_timer_net);    s_timer_net    = 0; }
}

GtkWidget *init_sidebar_popup(void) {
    GtkWidget *popup = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *surface = gtk_event_box_new();
    GtkWidget *content;

    gtk_widget_set_name(popup, "sidebar-popup");
    gtk_window_set_decorated(GTK_WINDOW(popup), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(popup), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(popup), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
    gtk_widget_set_app_paintable(popup, TRUE);

    panel_window_backend_init_popup(GTK_WINDOW(popup),
                                    "aether-sidebar-popup",
                                    GDK_WINDOW_TYPE_HINT_POPUP_MENU,
                                    GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    panel_window_backend_set_anchor(GTK_WINDOW(popup), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    panel_window_backend_set_anchor(GTK_WINDOW(popup), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    panel_window_backend_set_margin(GTK_WINDOW(popup), GTK_LAYER_SHELL_EDGE_TOP, 0);
    panel_window_backend_set_margin(GTK_WINDOW(popup), GTK_LAYER_SHELL_EDGE_LEFT, 8);
    ensure_rgba_visual(popup);
    g_signal_connect(popup, "draw", G_CALLBACK(draw_sidebar_popup_clear), NULL);
    ensure_rgba_visual(surface);
    gtk_widget_set_name(surface, "sidebar-popup-surface");
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(surface), TRUE);
    g_signal_connect(surface, "draw", G_CALLBACK(draw_sidebar_popup_background), NULL);

    content = create_sidebar_content();
    gtk_container_add(GTK_CONTAINER(surface), content);
    gtk_container_add(GTK_CONTAINER(popup), surface);

    /* Start persistent timers once here */
    sidebar_timers_start();

    gtk_widget_show_all(surface);
    gtk_widget_hide(popup);
    return popup;
}

void sidebar_popup_set_relative_to(GtkWidget *popup, GtkWidget *relative_to) {
    if (!popup) return;
    g_object_set_data(G_OBJECT(popup), "sidebar-relative-to", relative_to);
    reposition_sidebar_popup(popup, relative_to);
}

void sidebar_popup_toggle(GtkWidget *popup, GtkWidget *relative_to) {
    GtkWidget *anchor = relative_to;

    if (!popup) return;

    if (!anchor) {
        anchor = g_object_get_data(G_OBJECT(popup), "sidebar-relative-to");
    }

    if (anchor) {
        sidebar_popup_set_relative_to(popup, anchor);
    }

    if (gtk_widget_get_visible(popup)) {
        /* Phase 3: pause GPU-heavy timers while sidebar is hidden */
        sidebar_timers_stop();
        gtk_widget_hide(popup);
    } else {
        gtk_widget_show_all(popup);
        /* Phase 3: resume timers when sidebar becomes visible */
        sidebar_timers_start();
        /* Immediate refresh so values aren't stale after a long hide */
        update_clock(NULL);
        update_gauges(NULL);
    }
}
