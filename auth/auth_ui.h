#ifndef AUTH_UI_H
#define AUTH_UI_H

#define UI_RESULT_SUCCESS  1
#define UI_RESULT_CANCEL   0
#define UI_RESULT_ERROR   -1

int auth_ui_init(void);
void auth_ui_cleanup(void);

int auth_ui_get_password(const char *message,
                         const char *icon_name,
                         char *password_out,
                         int max_length);

#endif
