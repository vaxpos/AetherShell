#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <glib-unix.h>
#include <glib-object.h>
#include <polkitagent/polkitagent.h>

#include "auth_ui.h"

// ------------------------------------------------------------------------------------------------
// VenomAgent Class Definition (Polkit)
// ------------------------------------------------------------------------------------------------

#define VENOM_TYPE_AGENT (venom_agent_get_type())
G_DECLARE_FINAL_TYPE(VenomAgent, venom_agent, VENOM, AGENT, PolkitAgentListener)

struct _VenomAgent {
    PolkitAgentListener parent_instance;
};

G_DEFINE_TYPE(VenomAgent, venom_agent, POLKIT_AGENT_TYPE_LISTENER)

static void secure_clear(void *ptr, size_t len) {
    volatile unsigned char *p = ptr;
    while (len-- > 0) {
        *p++ = 0;
    }
}

static void secure_free_password(gpointer data) {
    if (!data) {
        return;
    }

    secure_clear(data, strlen((const char *)data));
    free(data);
}

static gpointer registration_handle = NULL;

static void venom_agent_init(VenomAgent *agent) {
    (void)agent;
}

static void on_completed(PolkitAgentSession *session,
                         gboolean            gained_authorization,
                         gpointer            user_data) {
    GTask *task = G_TASK(user_data);
    
    if (gained_authorization) {
        g_task_return_boolean(task, TRUE);
    } else {
        g_task_return_new_error(task, POLKIT_ERROR, POLKIT_ERROR_FAILED, "Authentication failed");
    }
    
    g_object_unref(session);
    g_object_unref(task);
}

static void on_request(PolkitAgentSession *session,
                       gchar              *request,
                       gboolean            echo_on,
                       gpointer            user_data) {
    (void)echo_on;
    (void)request;
    char *password = (char *)user_data;
    polkit_agent_session_response(session, password);
    secure_clear(password, strlen(password));
}

static void on_show_error(PolkitAgentSession *session,
                          gchar              *text,
                          gpointer            user_data) {
    (void)session;
    (void)user_data;
    (void)text;
}

static void on_show_info(PolkitAgentSession *session,
                         gchar              *text,
                         gpointer            user_data) {
    (void)session;
    (void)user_data;
    (void)text;
}

static void venom_agent_initiate_authentication_real(PolkitAgentListener  *listener,
                                                     const gchar          *action_id,
                                                     const gchar          *message,
                                                     const gchar          *icon_name,
                                                     PolkitDetails        *details,
                                                     const gchar          *cookie,
                                                     GList                *identities,
                                                     GCancellable         *cancellable,
                                                     GAsyncReadyCallback   callback,
                                                     gpointer              user_data) {
    (void)action_id;
    (void)icon_name;
    (void)details;

    VenomAgent *agent = VENOM_AGENT(listener);
    
    if (identities == NULL) {
        g_task_report_new_error(G_OBJECT(agent), callback, user_data, venom_agent_initiate_authentication_real,
                                POLKIT_ERROR, POLKIT_ERROR_FAILED, "No identities");
        return;
    }

    PolkitIdentity *identity = POLKIT_IDENTITY(identities->data);
    char *password = calloc(1, 512);
    if (!password) {
        g_task_report_new_error(G_OBJECT(agent), callback, user_data, venom_agent_initiate_authentication_real,
                                POLKIT_ERROR, POLKIT_ERROR_FAILED, "Out of memory");
        return;
    }

    int ui_result = auth_ui_get_password(
        message ? message : "Authentication Required",
        NULL,
        password,
        512
    );

    if (ui_result != UI_RESULT_SUCCESS) {
        secure_free_password(password);
        g_task_report_new_error(G_OBJECT(agent), callback, user_data, venom_agent_initiate_authentication_real,
                                POLKIT_ERROR, POLKIT_ERROR_CANCELLED, "Cancelled");
        return;
    }

    PolkitAgentSession *session = polkit_agent_session_new(identity, cookie);
    if (!session) {
        secure_free_password(password);
        g_task_report_new_error(G_OBJECT(agent), callback, user_data, venom_agent_initiate_authentication_real,
                                POLKIT_ERROR, POLKIT_ERROR_FAILED, "Failed to create Polkit session");
        return;
    }

    GTask *task = g_task_new(agent, cancellable, callback, user_data);
    
    g_signal_connect(session, "completed", G_CALLBACK(on_completed), task);
    g_signal_connect(session, "request", G_CALLBACK(on_request), password);
    g_signal_connect(session, "show-error", G_CALLBACK(on_show_error), NULL);
    g_signal_connect(session, "show-info", G_CALLBACK(on_show_info), NULL);

    g_object_set_data_full(G_OBJECT(session), "password", password, secure_free_password);

    polkit_agent_session_initiate(session);
}

static gboolean venom_agent_initiate_authentication_finish_real(PolkitAgentListener  *listener,
                                                                GAsyncResult         *res,
                                                                GError              **error) {
    (void)listener;
    return g_task_propagate_boolean(G_TASK(res), error);
}

static void venom_agent_class_init(VenomAgentClass *klass) {
    PolkitAgentListenerClass *listener_class = POLKIT_AGENT_LISTENER_CLASS(klass);
    
    listener_class->initiate_authentication = venom_agent_initiate_authentication_real;
    listener_class->initiate_authentication_finish = venom_agent_initiate_authentication_finish_real;
}

// ------------------------------------------------------------------------------------------------
// Main
// ------------------------------------------------------------------------------------------------

static GMainLoop *loop;

static gboolean on_unix_signal(gpointer user_data) {
    (void)user_data;
    if (loop) {
        g_main_loop_quit(loop);
    }

    return G_SOURCE_CONTINUE;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    // Initialize UI
    if (auth_ui_init() < 0) {
        fprintf(stderr, "Failed to initialize authentication UI\n");
        return 1;
    }
    
    // Create Polkit agent
    VenomAgent *agent = g_object_new(VENOM_TYPE_AGENT, NULL);
    if (!agent) {
        fprintf(stderr, "Failed to create Polkit agent\n");
        auth_ui_cleanup();
        return 1;
    }
    
    // Register Polkit agent
    gpointer subject = polkit_unix_session_new_for_process_sync(getpid(), NULL, NULL);
    if (!subject) {
        fprintf(stderr, "Failed to determine the current unix session\n");
        g_object_unref(agent);
        auth_ui_cleanup();
        return 1;
    }
    
    GError *error = NULL;
    registration_handle = polkit_agent_listener_register(POLKIT_AGENT_LISTENER(agent), 
                                                          POLKIT_AGENT_REGISTER_FLAGS_NONE,
                                                          subject, 
                                                          NULL, 
                                                          NULL, 
                                                          &error);
    if (!registration_handle) {
        fprintf(stderr, "Failed to register Polkit agent: %s\n", error->message);
        g_error_free(error);
        g_object_unref(agent);
        if (subject) {
            g_object_unref(subject);
        }
        auth_ui_cleanup();
        return 1;
    }
    
    fprintf(stdout, "Venom Auth started - Polkit agent active\n");
    
    loop = g_main_loop_new(NULL, FALSE);
    if (!loop) {
        fprintf(stderr, "Failed to create main loop\n");
        g_object_unref(agent);
        if (subject) {
            g_object_unref(subject);
        }
        auth_ui_cleanup();
        return 1;
    }

    g_unix_signal_add(SIGINT, on_unix_signal, NULL);
    g_unix_signal_add(SIGTERM, on_unix_signal, NULL);
    g_main_loop_run(loop);
    
    // Cleanup
    if (registration_handle) {
        polkit_agent_listener_unregister(registration_handle);
        registration_handle = NULL;
    }
    g_object_unref(agent);
    if (subject) {
        g_object_unref(subject);
    }
    g_main_loop_unref(loop);
    auth_ui_cleanup();
    
    return 0;
}
