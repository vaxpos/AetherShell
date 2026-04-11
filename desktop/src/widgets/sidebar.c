/*
 * sidebar.c — vaxp Desktop Widget
 *
 * One tall sidebar widget containing (top -> bottom):
 *   1. Digital Clock  (pink, large)
 *   2. Date           (e.g. "Sat, 02 December")
 *   3. Monthly Calendar  (interactive < > navigation)
 *   4. System Info    (compositor, shell, uptime, packages)
 *   5. RAM / Disk     (usage bars + used / total)
 *   6. Network        (IP/CIDR + upload / download sparklines)
 *   7. System Monitor (4 arc-ring gauges: CPU, Temp, GPU, RAM)
 *
 * Compile:
 *   gcc -shared -fPIC -O2 -o sidebar.so sidebar.c \
 *       $(pkg-config --cflags --libs gtk+-3.0) \
 *       -I/path/to/desktop/include -lm
 *
 * Install:
 *   cp sidebar.so ~/.config/vaxp/widgets/
 *
 */
#include <gtk/gtk.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/statvfs.h>
#include "../../include/vaxp-widget-api.h"
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
    snprintf(buf, sizeof(buf), "< %s >", MONTH_NAMES[view_month]);
    gtk_label_set_text(GTK_LABEL(lbl_month), buf);
    snprintf(buf, sizeof(buf), "< %d >", view_year);
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
        if (r && atoi(r)>0) { char *o=g_strdup_printf("%s packages",r); g_free(r); return o; }
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

static void read_vram_usage(void) {
    vram_used_mb = 0;
    vram_total_mb = 0;

    FILE *f = popen("nvidia-smi --query-gpu=memory.used,memory.total --format=csv,noheader,nounits 2>/dev/null", "r");
    if (f) {
        float used = 0, total = 0;
        char line[128] = {0};
        if (fgets(line, sizeof(line), f)) {
            for (char *p = line; *p; p++) if (*p == ',') *p = ' ';
            if (sscanf(line, "%f %f", &used, &total) == 2 && total > 0) {
                vram_used_mb = used;
                vram_total_mb = total;
                pclose(f);
                return;
            }
        }
        pclose(f);
    }

    for (int i = 0; i < 4; i++) {
        char used_path[256], total_path[256];
        snprintf(used_path, sizeof(used_path), "/sys/class/drm/card%d/device/mem_info_vram_used", i);
        snprintf(total_path, sizeof(total_path), "/sys/class/drm/card%d/device/mem_info_vram_total", i);
        long long used = read_ll_file_local(used_path);
        long long total = read_ll_file_local(total_path);
        if (used > 0 && total > 0) {
            vram_used_mb = used / (1024.0 * 1024.0);
            vram_total_mb = total / (1024.0 * 1024.0);
            return;
        }
    }
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
    return run_cmd(
        "ip -o -f inet addr show scope global | awk '{print $4}' | head -1");
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
static double get_cputemp_norm(void) {
    FILE *f = popen("cat /sys/class/thermal/thermal_zone*/temp 2>/dev/null | sort -n | tail -1", "r");
    if (!f) return 0;
    int t = 0;
    if (fscanf(f, "%d", &t) != 1) {
        pclose(f);
        return 0;
    }
    pclose(f);
    t /= 1000;
    snprintf(rings[1].label, sizeof(rings[1].label), "%d°C", t);
    return t > 0 ? t / 110.0 : 0;
}

static int read_int_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int v = -1;
    if (fscanf(f, "%d", &v) != 1) v = -1;
    fclose(f);
    return v;
}

static double get_gpu_usage_norm(void) {
    FILE *f = popen("nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
    if (f) {
        int load = 0;
        if (fscanf(f, "%d", &load) == 1 && load >= 0) {
            pclose(f);
            snprintf(rings[2].label, sizeof(rings[2].label), "%d%%", load);
            return load / 100.0;
        }
        pclose(f);
    }

    for (int i = 0; i < 4; i++) {
        char path[256];
        snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/gpu_busy_percent", i);
        int load = read_int_file(path);
        if (load >= 0) {
            snprintf(rings[2].label, sizeof(rings[2].label), "%d%%", load);
            return CLAMP(load / 100.0, 0.0, 1.0);
        }
    }

    return 0;
}

static gboolean update_gauges(gpointer _) {
    double cpu=get_cpu_pct(), ram=get_ram_pct();
    rings[0].val=cpu; snprintf(rings[0].label,sizeof(rings[0].label),"%.0f%%",cpu*100);
    rings[1].val=get_cputemp_norm();
    rings[2].val=get_gpu_usage_norm();
    rings[3].val=ram; snprintf(rings[3].label,sizeof(rings[3].label),"%.0f%%",ram*100);
    if(gauge_draw) gtk_widget_queue_draw(gauge_draw);
    return TRUE;
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
    int W=gtk_widget_get_allocated_width(w);
    cairo_set_source_rgba(cr,0,0,0,0); cairo_paint(cr);
    double cw=W/2.0, ch=68.0;
    double pos[4][2]={{cw*.5,ch*.5},{cw*1.5,ch*.5},{cw*.5,ch*1.5},{cw*1.5,ch*1.5}};
    for(int i=0;i<4;i++)
        draw_one_ring(cr,pos[i][0],pos[i][1],rings[i].val,
                      rings[i].r,rings[i].g,rings[i].b,rings[i].name,rings[i].label);
    return FALSE;
}

/* ═══════════════════════════════════════════════════════════════════
   GLOBAL CSS
   ═══════════════════════════════════════════════════════════════════ */
static const char *SIDEBAR_CSS =
    /* ── outer card ── */
    "#sidebar {"
    "  border-radius: 16px;"
    "  padding: 14px 10px 10px 10px;"
    "}"
    /* ── section divider ── */
    "#divider { margin: 6px 0; }"

    /* ── clock ── */
    "#lbl_time {"
    "  color: #ff79c6;"
    "  font-weight: 900;"
    "  font-size: 48px;"
    "  letter-spacing: 3px;"
    "}"
    "#lbl_date {"
    "  color: #cdd6f4;"
    "  font-size: 13px;"
    "  font-weight: 600;"
    "  margin-bottom: 4px;"
    "}"

    /* ── calendar nav ── */
    "#cal_nav_lbl {"
    "  color: #89dceb;"
    "  font-size: 12px;"
    "  font-weight: 700;"
    "}"
    "#cal_btn {"
    "  background: none; border: none;"
    "  color: #89dceb; font-size: 12px;"
    "  padding: 0 2px; min-width:0; min-height:0;"
    "}"
    "#cal_btn:hover { color: #cba6f7; }"

    /* ── calendar grid ── */
    "#cal_hdr  { color: #585b70; font-size: 11px; font-weight: 700; }"
    "#cal_day  { color: #cdd6f4; font-size: 11px; }"
    "#cal_fade { color: #3b3d52; font-size: 11px; }"
    "#cal_today{"
    "  color: #1e1e2e; background-color: #cba6f7;"
    "  border-radius: 50%; font-weight: 900; font-size: 11px;"
    "}"

    /* ── sysinfo ── */
    "#si_icon  { color: #89dceb; font-size: 12px; }"
    "#si_value { color: #cdd6f4; font-size: 12px; font-weight: 600; }"

    /* ── section title ── */
    "#sec_title {"
    "  color: #cba6f7;"
    "  font-size: 13px;"
    "  font-weight: 800;"
    "  letter-spacing: 1px;"
    "  margin: 4px 0 2px 0;"
    "}"
    "#stat_hdr {"
    "  color: rgba(130,140,200,0.75);"
    "  font-family: monospace;"
    "  font-size: 10px;"
    "  letter-spacing: 2px;"
    "}"
    "#stat_val {"
    "  color: rgba(200,210,255,0.90);"
    "  font-family: monospace;"
    "  font-size: 10.5px;"
    "}"

    /* ── net legend ── */
    "#net_up   { color: #f38ba8; font-size: 11px; }"
    "#net_down { color: #89b4fa; font-size: 11px; }";

/* ═══════════════════════════════════════════════════════════════════
   DRAG SUPPORT
   ═══════════════════════════════════════════════════════════════════ */
static gboolean is_drag=FALSE;
static int dsx,dsy,wsx,wsy;
static vaxpDesktopAPI *g_api=NULL;

static GtkCssProvider *bg_css = NULL;

static void set_theme(const char *hex_color, double opacity) {
    if (!bg_css) return;
    GdkRGBA rgba;
    if (!gdk_rgba_parse(&rgba, hex_color)) gdk_rgba_parse(&rgba, "#0e0e16");
    char op_str[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_dtostr(op_str, sizeof(op_str), opacity);
    char *css = g_strdup_printf("#sidebar { background-color: rgba(%d, %d, %d, %s); }",
        (int)(rgba.red*255), (int)(rgba.green*255), (int)(rgba.blue*255), op_str);
    gtk_css_provider_load_from_data(bg_css, css, -1, NULL);
    g_free(css);
}

static gboolean on_press(GtkWidget *w, GdkEventButton *e, gpointer _) {
    if (e->button!=1) return FALSE;
    is_drag=TRUE; dsx=e->x_root; dsy=e->y_root;
    gint wx,wy;
    gtk_widget_translate_coordinates(w,gtk_widget_get_toplevel(w),0,0,&wx,&wy);
    wsx=wx; wsy=wy; return TRUE;
}
static gboolean on_motion(GtkWidget *w, GdkEventMotion *e, gpointer _) {
    if(!is_drag||!g_api||!g_api->layout_container) return FALSE;
    GtkWidget *target = w;
    while (target && gtk_widget_get_parent(target) != g_api->layout_container) {
        target = gtk_widget_get_parent(target);
    }
    if (target) {
        gtk_layout_move(GTK_LAYOUT(g_api->layout_container), target,
            wsx+(int)(e->x_root-dsx), wsy+(int)(e->y_root-dsy));
    }
    return TRUE;
}
static gboolean on_release(GtkWidget *w, GdkEventButton *e, gpointer _) {
    if(e->button!=1||!is_drag) return FALSE;
    is_drag=FALSE;
    if(g_api&&g_api->save_position){
        gint x,y;
        gtk_widget_translate_coordinates(w,gtk_widget_get_toplevel(w),0,0,&x,&y);
        g_api->save_position("sidebar.so",x,y);
    }
    return TRUE;
}

/* ═══════════════════════════════════════════════════════════════════
   HELPER: thin horizontal separator
   ═══════════════════════════════════════════════════════════════════ */
static GtkWidget* make_sep(void) {
    GtkWidget *s = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_name(s,"divider"); return s;
}

/* ═══════════════════════════════════════════════════════════════════
   WIDGET FACTORY
   ═══════════════════════════════════════════════════════════════════ */
static guint t_clock=0, t_sysinfo=0, t_storage=0, t_gauges=0, t_net=0;

static GtkWidget* create_widget(vaxpDesktopAPI *api) {
    g_api = api;
    memset(ul_hist,0,sizeof(ul_hist));
    memset(dl_hist,0,sizeof(dl_hist));

    /* ── init calendar view to current month ── */
    time_t now0=time(NULL); struct tm *t0=localtime(&now0);
    view_year=t0->tm_year+1900; view_month=t0->tm_mon;

    /* ── CSS ── */
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,SIDEBAR_CSS,-1,NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    /* ── root event box ── */
    GtkWidget *ebox = gtk_event_box_new();
    gtk_widget_set_events(ebox,
        GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_POINTER_MOTION_MASK);
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox),FALSE);
    g_signal_connect(ebox,"button-press-event",  G_CALLBACK(on_press),  NULL);
    g_signal_connect(ebox,"motion-notify-event", G_CALLBACK(on_motion), NULL);
    g_signal_connect(ebox,"button-release-event",G_CALLBACK(on_release),NULL);

    /* ── main card ── */
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL,4);
    gtk_widget_set_name(card,"sidebar");
    gtk_widget_set_size_request(card,220,-1);
    gtk_container_add(GTK_CONTAINER(ebox),card);

    GtkStyleContext *context = gtk_widget_get_style_context(card);
    bg_css = gtk_css_provider_new();
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(bg_css), 801);
    set_theme("#0e0e16", 0.92); /* default */

    /* ─────────── 1. CLOCK ─────────── */
    lbl_time = gtk_label_new("00:00");
    gtk_widget_set_name(lbl_time,"lbl_time");
    gtk_label_set_xalign(GTK_LABEL(lbl_time),0.5);
    gtk_box_pack_start(GTK_BOX(card),lbl_time,FALSE,FALSE,0);

    lbl_date = gtk_label_new("");
    gtk_widget_set_name(lbl_date,"lbl_date");
    gtk_label_set_xalign(GTK_LABEL(lbl_date),0.5);
    gtk_box_pack_start(GTK_BOX(card),lbl_date,FALSE,FALSE,0);

    gtk_box_pack_start(GTK_BOX(card),make_sep(),FALSE,FALSE,2);

    /* ─────────── 2. CALENDAR ─────────── */
    /* month nav */
    GtkWidget *nav_m = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_widget_set_halign(nav_m,GTK_ALIGN_CENTER);
    GtkWidget *bpm=gtk_button_new_with_label("<"); gtk_widget_set_name(bpm,"cal_btn");
    g_signal_connect(bpm,"clicked",G_CALLBACK(on_prev_month),NULL);
    lbl_month=gtk_label_new(""); gtk_widget_set_name(lbl_month,"cal_nav_lbl");
    GtkWidget *bnm=gtk_button_new_with_label(">"); gtk_widget_set_name(bnm,"cal_btn");
    g_signal_connect(bnm,"clicked",G_CALLBACK(on_next_month),NULL);
    gtk_box_pack_start(GTK_BOX(nav_m),bpm,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(nav_m),lbl_month,FALSE,FALSE,4);
    gtk_box_pack_start(GTK_BOX(nav_m),bnm,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(card),nav_m,FALSE,FALSE,0);

    /* year nav */
    GtkWidget *nav_y = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_widget_set_halign(nav_y,GTK_ALIGN_CENTER);
    GtkWidget *bpy=gtk_button_new_with_label("<"); gtk_widget_set_name(bpy,"cal_btn");
    g_signal_connect(bpy,"clicked",G_CALLBACK(on_prev_year),NULL);
    lbl_year=gtk_label_new(""); gtk_widget_set_name(lbl_year,"cal_nav_lbl");
    GtkWidget *bny=gtk_button_new_with_label(">"); gtk_widget_set_name(bny,"cal_btn");
    g_signal_connect(bny,"clicked",G_CALLBACK(on_next_year),NULL);
    gtk_box_pack_start(GTK_BOX(nav_y),bpy,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(nav_y),lbl_year,FALSE,FALSE,4);
    gtk_box_pack_start(GTK_BOX(nav_y),bny,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(card),nav_y,FALSE,FALSE,0);

    /* grid */
    cal_grid=gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(cal_grid),TRUE);
    gtk_grid_set_row_spacing(GTK_GRID(cal_grid),1);
    gtk_widget_set_halign(cal_grid,GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(card),cal_grid,FALSE,FALSE,4);

    gtk_box_pack_start(GTK_BOX(card),make_sep(),FALSE,FALSE,2);

    /* ─────────── 3. SYSTEM INFO ─────────── */
    /* helper lambda-like macro for one info row */
    #define SI_ROW(icon, lbl_ptr) { \
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6); \
        GtkWidget *ic  = gtk_label_new(icon); gtk_widget_set_name(ic,"si_icon"); \
        gtk_label_set_xalign(GTK_LABEL(ic),0.0); gtk_widget_set_size_request(ic,22,-1); \
        GtkWidget *sep = gtk_label_new(":"); gtk_widget_set_name(sep,"si_icon"); \
        lbl_ptr = gtk_label_new("..."); gtk_widget_set_name(lbl_ptr,"si_value"); \
        gtk_label_set_xalign(GTK_LABEL(lbl_ptr),0.0); \
        gtk_box_pack_start(GTK_BOX(row),ic,FALSE,FALSE,0); \
        gtk_box_pack_start(GTK_BOX(row),sep,FALSE,FALSE,0); \
        gtk_box_pack_start(GTK_BOX(row),lbl_ptr,TRUE,TRUE,0); \
        gtk_box_pack_start(GTK_BOX(card),row,FALSE,FALSE,0); \
    }
    SI_ROW("🪟", lbl_compositor)
    SI_ROW("🐟", lbl_shell)
    SI_ROW("🕐", lbl_uptime)
    SI_ROW("📦", lbl_packages)
    #undef SI_ROW

    { char *c=get_compositor(),*s=get_shell(),*u=get_uptime_str(),*p=get_packages();
      gtk_label_set_text(GTK_LABEL(lbl_compositor),c);
      gtk_label_set_text(GTK_LABEL(lbl_shell),s);
      gtk_label_set_text(GTK_LABEL(lbl_uptime),u);
      gtk_label_set_text(GTK_LABEL(lbl_packages),p);
      g_free(c); g_free(s); g_free(u); g_free(p); }

    gtk_box_pack_start(GTK_BOX(card),make_sep(),FALSE,FALSE,2);

    /* ─────────── 4. RAM / DISK ─────────── */
    GtkWidget *storage_title = gtk_label_new("Storage");
    gtk_widget_set_name(storage_title,"sec_title");
    gtk_label_set_xalign(GTK_LABEL(storage_title),0.5);
    gtk_box_pack_start(GTK_BOX(card),storage_title,FALSE,FALSE,0);

    GtkWidget *ram_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
    GtkWidget *ram_hdr = gtk_label_new("RAM");
    gtk_widget_set_name(ram_hdr,"stat_hdr");
    gtk_label_set_xalign(GTK_LABEL(ram_hdr),0.0);
    lbl_ram_usage = gtk_label_new("-- / --");
    gtk_widget_set_name(lbl_ram_usage,"stat_val");
    gtk_label_set_xalign(GTK_LABEL(lbl_ram_usage),1.0);
    gtk_box_pack_start(GTK_BOX(ram_row),ram_hdr,FALSE,FALSE,0);
    gtk_box_pack_end(GTK_BOX(ram_row),lbl_ram_usage,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(card),ram_row,FALSE,FALSE,0);

    ram_bar_draw = gtk_drawing_area_new();
    gtk_widget_set_size_request(ram_bar_draw,-1,10);
    g_signal_connect(ram_bar_draw,"draw",G_CALLBACK(on_draw_ram_bar),NULL);
    gtk_box_pack_start(GTK_BOX(card),ram_bar_draw,FALSE,FALSE,0);

    GtkWidget *disk_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
    GtkWidget *disk_hdr = gtk_label_new("DISK /");
    gtk_widget_set_name(disk_hdr,"stat_hdr");
    gtk_label_set_xalign(GTK_LABEL(disk_hdr),0.0);
    lbl_disk_usage = gtk_label_new("-- / --");
    gtk_widget_set_name(lbl_disk_usage,"stat_val");
    gtk_label_set_xalign(GTK_LABEL(lbl_disk_usage),1.0);
    gtk_box_pack_start(GTK_BOX(disk_row),disk_hdr,FALSE,FALSE,0);
    gtk_box_pack_end(GTK_BOX(disk_row),lbl_disk_usage,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(card),disk_row,FALSE,FALSE,0);

    disk_bar_draw = gtk_drawing_area_new();
    gtk_widget_set_size_request(disk_bar_draw,-1,10);
    g_signal_connect(disk_bar_draw,"draw",G_CALLBACK(on_draw_disk_bar),NULL);
    gtk_box_pack_start(GTK_BOX(card),disk_bar_draw,FALSE,FALSE,0);

    GtkWidget *vram_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
    GtkWidget *vram_hdr = gtk_label_new("VRAM");
    gtk_widget_set_name(vram_hdr,"stat_hdr");
    gtk_label_set_xalign(GTK_LABEL(vram_hdr),0.0);
    lbl_vram_usage = gtk_label_new("-- / --");
    gtk_widget_set_name(lbl_vram_usage,"stat_val");
    gtk_label_set_xalign(GTK_LABEL(lbl_vram_usage),1.0);
    gtk_box_pack_start(GTK_BOX(vram_row),vram_hdr,FALSE,FALSE,0);
    gtk_box_pack_end(GTK_BOX(vram_row),lbl_vram_usage,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(card),vram_row,FALSE,FALSE,0);

    vram_bar_draw = gtk_drawing_area_new();
    gtk_widget_set_size_request(vram_bar_draw,-1,10);
    g_signal_connect(vram_bar_draw,"draw",G_CALLBACK(on_draw_vram_bar),NULL);
    gtk_box_pack_start(GTK_BOX(card),vram_bar_draw,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(card),make_sep(),FALSE,FALSE,2);

    /* ─────────── 5. NETWORK ─────────── */
    lbl_ip=gtk_label_new("...");
    gtk_widget_set_name(lbl_ip,"si_value");
    gtk_label_set_xalign(GTK_LABEL(lbl_ip),0.5);
    gtk_box_pack_start(GTK_BOX(card),lbl_ip,FALSE,FALSE,0);
    { char *ip=get_ip(); gtk_label_set_text(GTK_LABEL(lbl_ip),ip); g_free(ip); }

    GtkWidget *net_leg=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    gtk_widget_set_halign(net_leg,GTK_ALIGN_CENTER);

    GtkWidget *ul_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    GtkWidget *ul_lbl = gtk_label_new("↑");
    gtk_widget_set_name(ul_lbl,"net_up");
    lbl_ul_speed = gtk_label_new("-- KB/s");
    gtk_widget_set_name(lbl_ul_speed,"net_up");
    gtk_box_pack_start(GTK_BOX(ul_box),ul_lbl,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(ul_box),lbl_ul_speed,FALSE,FALSE,0);

    GtkWidget *dl_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    GtkWidget *dl_lbl = gtk_label_new("↓");
    gtk_widget_set_name(dl_lbl,"net_down");
    lbl_dl_speed = gtk_label_new("-- KB/s");
    gtk_widget_set_name(lbl_dl_speed,"net_down");
    gtk_box_pack_start(GTK_BOX(dl_box),dl_lbl,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(dl_box),lbl_dl_speed,FALSE,FALSE,0);

    gtk_box_pack_start(GTK_BOX(net_leg),ul_box,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(net_leg),dl_box,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(card),net_leg,FALSE,FALSE,0);

    net_draw=gtk_drawing_area_new();
    gtk_widget_set_size_request(net_draw,200,70);
    g_signal_connect(net_draw,"draw",G_CALLBACK(on_net_draw),NULL);
    gtk_box_pack_start(GTK_BOX(card),net_draw,FALSE,FALSE,0);

    gtk_box_pack_start(GTK_BOX(card),make_sep(),FALSE,FALSE,2);

    /* ─────────── 6. SYSTEM MONITOR ─────────── */
    gauge_draw=gtk_drawing_area_new();
    gtk_widget_set_size_request(gauge_draw,200,145);
    g_signal_connect(gauge_draw,"draw",G_CALLBACK(on_gauge_draw),NULL);
    gtk_box_pack_start(GTK_BOX(card),gauge_draw,FALSE,FALSE,0);

    /* ─────────── TIMERS ─────────── */
    update_clock(NULL);
    rebuild_calendar();
    update_storage(NULL);
    update_gauges(NULL);

    t_clock   = g_timeout_add(1000,  update_clock,    NULL);
    t_sysinfo = g_timeout_add(60000, refresh_sysinfo, NULL);
    t_storage = g_timeout_add(2000,  update_storage,  NULL);
    t_gauges  = g_timeout_add(2000,  update_gauges,   NULL);
    t_net     = g_timeout_add(1000,  sample_net,      NULL);

    gtk_widget_show_all(ebox);
    return ebox;
}

static void destroy_widget(void) {
    if (t_clock)   { g_source_remove(t_clock);   t_clock = 0; }
    if (t_sysinfo) { g_source_remove(t_sysinfo); t_sysinfo = 0; }
    if (t_storage) { g_source_remove(t_storage); t_storage = 0; }
    if (t_gauges)  { g_source_remove(t_gauges);  t_gauges = 0; }
    if (t_net)     { g_source_remove(t_net);     t_net = 0; }
}

/* ═══════════════════════════════════════════════════════════════════
   ENTRY POINT
   ═══════════════════════════════════════════════════════════════════ */
vaxpWidgetAPI* vaxp_widget_init(void) {
    static vaxpWidgetAPI api;
    api.name           = "Sidebar";
    api.description    = "All-in-one sidebar: clock, calendar, sysinfo, RAM/disk, network, gauges.";
    api.author         = "vaxp Core";
    api.create_widget  = create_widget;
    api.update_theme   = set_theme;
    api.destroy_widget = destroy_widget;
    return &api;
}
