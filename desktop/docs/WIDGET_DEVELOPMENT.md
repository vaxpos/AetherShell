# دليل المطورين: إنشاء وإضافة الودجتس لمدير سطح المكتب (vaxp Desktop Manager)

يوفر **vaxp Desktop Manager** نظام إضافات ديناميكية (Dynamic Plugins) مبني على مكتبات المشتركة (`.so`). هذا يعني أن أي مطور يمكنه بناء أداة تفاعلية (Widget) بلغة C و GTK وإضافتها لسطح المكتب دون الحاجة لتعديل أو إعادة تجميع (Compile) الكود المصدري لمدير سطح المكتب نفسه.

هذا الدليل يشرح كيفية بناء ودجت من الصفر.

---

## 1. الواجهة البرمجية (The API)

لبناء ودجت، يجب أن يحتوي كودك المصدري على واجهة موحدة `vaxpWidgetAPI` يتعرف عليها مدير سطح المكتب.
يتم تعريف هذه الواجهة في ملف `vaxp-widget-api.h` الذي يجب تضمينه في مشروعك.

```c
#include <gtk/gtk.h>

/* ವಾجهة يمررها مدير سطح المكتب للودجت */
typedef struct {
    GtkWidget *layout_container; /* حاوية سطح المكتب، تفيد لتمرير حركات الفأرة للحاوية الأم */
    void (*save_position)(const char *widget_name, int x, int y); /* دالة حفظ إحداثيات الودجت */
} vaxpDesktopAPI;

/* الواجهة التي يجب على الودجت الخاص بك إرجاعها */
typedef struct {
    const char *name;         /* اسم الودجت (مثلاً: "System Monitor") */
    const char *description;  /* وصف الودجت */
    const char *author;       /* اسم المطور */
    
    /* دالة التهيئة (Initialization):
     * تُنادى هذه الدالة مرة واحدة عند تحميل الودجت.
     * يجب أن تقوم هذه الدالة ببناء واجهة GTK الخاصة بك وإرجاع الحاوية الرئيسية (GtkWidget*).
     */
    GtkWidget* (*create_widget)(vaxpDesktopAPI *desktop_api);
} vaxpWidgetAPI;
```

---

## 2. الخطوات الأساسية لكتابة الودجت

### أ. بناء واجهة الـ GTK وتصدير الدالة الأساسية
مدير سطح المكتب يبحث ديناميكياً باستخدام (`dlsym`) عن دالة محددة داخل ملف الـ `.so` الخاص بك تدعى `vaxp_widget_init`. **يجب** أن تكون هذه الدالة موجودة وتُرجع (pointer) لهيكل `vaxpWidgetAPI`.

**مثال تطبيقي (ملف `my-clock.c`):**

```c
#include <gtk/gtk.h>
#include <time.h>
#include "vaxp-widget-api.h" // تأكد من الحصول على هذا الملف من مصدر مدير سطح المكتب

/* ودجت الساعة الخاص بنا */
static GtkWidget *clock_label = NULL;

/* تحديث الساعة كل ثانية */
static gboolean update_time(gpointer data) {
    if (!clock_label) return FALSE;
    time_t rawtime;
    struct tm *info;
    char buffer[80];
    time(&rawtime);
    info = localtime(&rawtime);
    strftime(buffer, 80, "%I:%M:%S %p", info);
    gtk_label_set_text(GTK_LABEL(clock_label), buffer);
    return TRUE; /* استمر في التحديث */
}

/* دالة التهيئة (سيستدعيها مدير سطح المكتب) */
static GtkWidget* my_clock_create(vaxpDesktopAPI *api) {
    // 1. بناء الحاوية (يفضل استخدام EventBox لدعم السحب والإفلات مستقبلاً)
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    // 2. تصميم الواجهة
    clock_label = gtk_label_new("00:00:00 AM");
    // يمكن هنا إضافة تنسيقات CSS مخصصة (GtkCssProvider)
    gtk_box_pack_start(GTK_BOX(box), clock_label, TRUE, TRUE, 10);
    
    // 3. تشغيل المؤقت (Timer) لتحديث الواجهة في الخلفية دون إيقاف سطح المكتب
    g_timeout_add(1000, update_time, NULL);
    
    gtk_widget_show_all(box);
    return box; // نعيد واجهتنا للمدير ليقوم بوضعها على الشاشة
}

/* هذه هي الدالة الوحيدة التي يقرأها مدير سطح المكتب */
vaxpWidgetAPI* vaxp_widget_init(void) {
    static vaxpWidgetAPI my_plugin;
    my_plugin.name = "My Custom Clock";
    my_plugin.description = "A simple desktop clock.";
    my_plugin.author = "You";
    my_plugin.create_widget = my_clock_create;
    return &my_plugin;
}
```

---

## 3. الترجمة (Compiling)
بما أن الودجت هو مكتبة ديناميكية، فيجب ترجمة ملف הـ `.c` باستخدام رايات `-shared` و `-fPIC` (Position Independent Code) كالتالي:

```bash
gcc -shared -fPIC -o my-clock.so my-clock.c $(pkg-config --cflags --libs gtk+-3.0) -I/path/to/desktop/include
```
*(ملاحظة: تأكد من تغيير `-I/path/to/desktop/include` ليتوافق مع مسار ملف `vaxp-widget-api.h` في جهازك).*

**إذا كنت تقوم بالتطوير داخل مجلد المصدر (Source tree) الخاص بـ vaxp Desktop:**
المشروع جاهز تماماً، ما عليك سوى وضع ملف `my-clock.c` داخل مجلد `src/widgets/`، ثم تشغيل:
```bash
make widgets
```
وسيقوم الـ Makefile تلقائيا بترجمته ووضعه في المسار الصحيح!

---

## 4. مسار التثبيت (Installation Directory)
لكي يتعرف `desktop` على الودجت الخاص بك ويعرضه، يجب نقل ملف `my-clock.so` النهائي إلى مجلد إعدادات التطبيق الخاص بالمستخدم:

```text
~/.config/vaxp/widgets/
```
أي: `/home/YOUR_USERNAME/.config/vaxp/widgets/`

عندما يقوم المستخدم بتبديل سطح المكتب إلى **"وضع الـ Widgets" (Widgets Only)** سيقرأ التطبيق هذا المسار، ويستخرج واجهتك، ويعرضها مباشرة!

---

## 5. دعم خاصية السحب والإفلات (اختياري - متطور)
لكي تمكن المستخدم من النقر على الودجت الخاص بك وتحريكه على الشاشة، ستحتاج لاستخدام المعامل `desktop_api` الذي يُمرر لكلال دالة `create_widget`.
يجب عليك إنشاء `GtkEventBox` كحاوية رئيسية والتقاط أحداث الفأرة (`button-press-event` و `motion-notify-event` و `button-release-event`).

عندما يتحرك الماوس، يمكنك نداء `gtk_layout_move(api->layout_container, widget, x, y)` لتحريك الودجت، وعند إفلات الماوس تنادي `api->save_position("my-clock.so", x, y)` لحفظ الإحداثيات الجديدة في إعدادات النظام. يمكنك الرجوع للكود المصدري لودجت مراقب النظام (`src/widgets/sysmon.c`) كمثال عملي ممتاز لذلك.

---

## 6. دعم تغيير لون وخلفية الودجتس (Widget Theming API)
بإمكانك دعم ميزة تغيير لون خلفية وشفافية الودجت ديناميكياً من خلال واجهة `update_theme`.
يقوم مدير سطح المكتب بمنح المستخدم القدرة على تخصيص ألوان الودجتس، وكل ما عليك فعله هو توفير دالة للتعامل مع هذا التغيير.

**كيفية دعم الخاصية:**
1. قُم بإنشاء `GtkCssProvider` مخصص لخلفية الودجت الخاص بك (`bg_css`).
2. أضف دالة تستقبل اللون (بصيغة Hex) والشفافية (Opacity) وتقوم بتوليد كود CSS وتطبيقه على الـ Provider.
3. قم بتمرير هذه الدالة لمؤشر `api.update_theme` في الواجهة المُرجَعة.

**مثال عملي:**
```c
static GtkCssProvider *bg_css = NULL;

static void set_theme(const char *hex_color, double opacity) {
    if (!bg_css) return;
    GdkRGBA rgba;
    if (!gdk_rgba_parse(&rgba, hex_color)) gdk_rgba_parse(&rgba, "#000000"); // لون افتراضي
    
    // تحويل الشفافية لنص متوافق مع كافة اللغات (Locales)
    char op_str[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_dtostr(op_str, sizeof(op_str), opacity);
    
    // توليد كود CSS (تأكد من اختيار المُعرّف أو العنصر الصحيح مثل frame أو box)
    char *css = g_strdup_printf("frame { background-color: rgba(%d, %d, %d, %s); }",
        (int)(rgba.red * 255), (int)(rgba.green * 255), (int)(rgba.blue * 255), op_str);
        
    gtk_css_provider_load_from_data(bg_css, css, -1, NULL);
    g_free(css);
}

static GtkWidget* my_widget_create(vaxpDesktopAPI *api) {
    GtkWidget *frame = gtk_frame_new(NULL);
    // ... إعداد الودجت ...
    
    // إعداد مزود الـ CSS وتعيين الثيم الافتراضي
    GtkStyleContext *context = gtk_widget_get_style_context(frame);
    bg_css = gtk_css_provider_new();
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(bg_css), 801);
    set_theme("#000000", 0.5); 
    
    return frame;
}

vaxpWidgetAPI* vaxp_widget_init(void) {
    static vaxpWidgetAPI my_plugin;
    // ... إعداد بقية التفاصيل ...
    my_plugin.create_widget = my_widget_create;
    my_plugin.update_theme = set_theme; /* توجيه الدالة للواجهة */
    return &my_plugin;
}
```
*ملاحظة: تأكد من استخدام `g_ascii_dtostr` لتحويل الفواصل العشرية للشفافية حتى تعمل الخاصية على جميع لغات النظام المختلفة بأمان، وأيضاً تخصيص مزود CSS للـ `background` فقط لكي لا يؤثر على ألوان النصوص والأيقونات.*

---

## 7. مثال جاهز: ودجت الطقس (Weather)
يوجد مثال ودجت طقس جاهز داخل المشروع: `src/widgets/weather.c`.

- مصدر البيانات: `wttr.in` (يحتاج إنترنت + وجود `curl` على النظام).
- التحديث: تلقائياً كل 10 دقائق، ويمكن التحديث فوراً بـ **Right Click** على الودجت.
- الإعدادات: أنشئ ملف:
  - `~/.config/vaxp/weather.conf`
  - واكتب داخله:
    - `location=Baghdad` (غيّرها لمدينتك)

ملاحظة للمطورين: يمكن تغيير مكان إخراج ملفات الودجت عند البناء عبر:
```bash
make widgets WIDGET_INSTALL_DIR=obj/widgets
```
