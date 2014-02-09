/*
 * pwrnotify version 0.2.1-next
 *
 * a lightweight, standalone battery status notifier
 *
 * Licensed under the GNU General Public License, version 3; if this was not
 * included, you can find it here:
 *     http://www.gnu.org/licenses/gpl-3.0.txt
 */

// TODO:
// (getopt/getopt_long)
// -d --delay (seconds between each check)
// -r --reload (seconds between each recheck for batteries)
// -b --background (fork off and die)
// -l --legacy (use /proc/acpi/ instead of /sys/class/)

#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <libnotify/notify.h>

#include <glib-object.h>
#include <glib.h>

static const char* version = "0.2.1-next";

struct state_type {
    int nwarn;
    char* warn;
    int nbats;
    char* bat_names;
    gulong* handler_id;
    char* fn;
    char body[26];
    char maxwarn;
    char last;
    NotifyNotification* notification;
    char* closed;
};

gboolean get_bats (struct state_type* state) {
    // get available power supplies
    if (state->bat_names != (char*) NULL) free(state->bat_names);
    if (state->fn != (char*) NULL) free(state->fn);

    DIR* dir = opendir("/sys/class/power_supply/");
    if (dir == (DIR*) NULL) {
        state->nbats = 0;
        return G_SOURCE_CONTINUE;
    }
    struct dirent* entry = readdir(dir);
    struct dirent* next;
    int err;
    char buf[7], * fn;
    const int mem_blk = 50; // chars to allocate at a time
    int n, nchars = 0, nalloc = 0, n_max = 0;
    state->bat_names = (char*) NULL;
    state->nbats = 0;

    while (entry != (struct dirent*) NULL) {
        // skip if . or ..
        if (strcmp(entry->d_name, ".") != 0 &&
            strcmp(entry->d_name, "..") != 0) {
            n = strlen(entry->d_name);
            // check supply's type
            fn = malloc((24 + n + 5 + 1) * sizeof(char));
            sprintf(fn, "/sys/class/power_supply/%s/type", entry->d_name);
            FILE* f = fopen(fn, "r");
            free(fn);
            if (f != (FILE*) NULL) {
                fread(buf, sizeof(char), 7, f);
                fclose(f);
                // skip if not a battery
                if (strncmp(buf, "Battery", 7) == 0) { // buf has no \0
                    (state->nbats)++;
                    while (nchars + n + 1 > nalloc) {
                        // need more room for names
                        nalloc += mem_blk;
                        state->bat_names = realloc(
                            state->bat_names, nalloc * sizeof(char)
                        );
                    }
                    strcpy(state->bat_names + nchars, entry->d_name);
                    nchars += n + 1;
                    if (n > n_max) n_max = n;
                }
            }
        }
        err = readdir_r(dir, entry, &next);
        if (err != 0) entry = (struct dirent*) NULL;
        entry = next;
    }

    closedir(dir);
    state->fn = malloc((24 + n_max + 12 + 1) * sizeof(char));
    return G_SOURCE_CONTINUE;
}

int get_charge_from_file (char* bat_name, char* file_name, char* fn) {
    sprintf(fn, "/sys/class/power_supply/%s/charge_%s", bat_name, file_name);
    FILE* f = fopen(fn, "r");
    int q = 0;
    if (f != (FILE*) NULL) {
        fscanf(f, "%d", &q); // only sets value if no errors
        fclose(f);
    }
    return q;
}

int get_charge (int n, char* bat_names, char* fn) {
    // get total battery charge as integer percentage of maximum
    int i, q = 0, qtot = 0;
    for (i = 0; i < n; i++) {
        q += get_charge_from_file(bat_names, "now", fn);
        qtot += get_charge_from_file(bat_names, "full", fn);
        bat_names += strlen(bat_names);
    }
    if (qtot == 0) return 100;
    else if (q > qtot) return 100;
    else return (q * 100) / qtot;
}

void notification_closed (NotifyNotification* notification, char* closed) {
    *closed = 1;
}

void notification_init (NotifyNotification** notification,
                        gulong** handler_id, char* closed) {
    notify_init("pwrnotify");
    *notification = notify_notification_new("", "", "");
    notify_notification_set_timeout(*notification, NOTIFY_EXPIRES_NEVER);
    if (*handler_id == (gulong*) NULL) {
        *handler_id = malloc(sizeof(gulong));
        **handler_id = g_signal_connect(
            *notification, "closed", G_CALLBACK(notification_closed), closed
        );
    }
}

int notification_show (NotifyNotification* notification, char* body, int q) {
    // should have 0 <= q <= 100, but use snprintf just in case
    snprintf(body, 26, "Battery level is at %d%%.\n", q);
    notify_notification_update(notification, "Low Battery", body,
                               "dialog-warning");
    GError* e = (GError*) NULL;
    if (!notify_notification_show(notification, &e)) {
        fprintf(stderr, "error: %s\n", e->message);
        return 0;
    } else {
        return 1;
    }
}

void notification_uninit (NotifyNotification** notification,
                          gulong** handler_id) {
    notify_notification_close(*notification, (GError**) NULL);
    if (*handler_id != (gulong*) NULL) {
        g_signal_handler_disconnect(*notification, **handler_id);
        free(*handler_id);
        *handler_id = (gulong*) NULL;
    }
    g_object_unref(G_OBJECT(*notification));
    notify_uninit();
}

void notification_try_show (NotifyNotification* notification, char* body,
                            int q, gulong** handler_id, char* closed) {
    // notification seems to need destroying and recreating if unused for a
    // long time
    if (!notification_show(notification, body, q)) {
        notification_uninit(&notification, handler_id);
        notification_init(&notification, handler_id, closed);
        if (!notification_show(notification, body, q)) {
            fprintf(stderr, "error: could not show notification\n");
        }
    }
}

gboolean check_bats (struct state_type* state) {
    char q = get_charge(state->nbats, state->bat_names, state->fn), updated;
    int i;
    if (state->last != q) {
        updated = 0;
        for (i = 0; i < state->nwarn; i++) {
            if (state->last >= state->warn[i] && q < state->warn[i]) {
                // dipped below a warning level: show notification
                notification_try_show(state->notification, state->body, q,
                                      &state->handler_id, state->closed);
                *state->closed = 0;
                // already shown: no need to check other warning levels
                updated = 1;
                break;
            }
        }
        if (state->last < state->maxwarn && q >= state->maxwarn) {
            // newly above all warning levels: hide notification
            notify_notification_close(state->notification, (GError**) NULL);
        } else if (!updated && !*state->closed) {
            notification_try_show(state->notification, state->body, q,
                                  &state->handler_id, state->closed);
        }
        state->last = q;
    }
    return G_SOURCE_CONTINUE;
}

int main (int argc, char** argv) {
    // parse arguments
    if (argc == 1) {
        fprintf(stderr, "pwrnotify %s \n\
\n\
Takes any number of integer arguments as percentage battery levels to \n\
display a warning at.\n\
\n\
Returns 2 if invalid arguments are given.\n\n", version);
        return 0;
    }
    // read in warning levels; duplicates aren't a problem so don't bother
    // checking for them
    int err, i, j, n, val, nwarn = argc - 1;
    char* warn = malloc(nwarn * sizeof(char)), * arg;
    for (i = 0; i < nwarn; i++) {
        err = 0;
        arg = argv[i + 1];
        n = strlen(arg);
        if (n == 0) err = 1;
        else {
            for (j = 0; j < n; j++) {
                if (!isdigit(arg[j])) {
                    err = 1;
                    break;
                }
            }
        }
        if (!err && sscanf(arg, "%d", &val) != 1) err = 1;
        if (err) {
            fprintf(stderr, "invalid argument '%s' (expected int)\n", arg);
            return 2;
        }
        if (val < 0 || val > 100) {
            fprintf(stderr, "invalid argument '%d' (expected percentage)\n",
                    val);
            return 2;
        }
        warn[i] = val;
    }

    // set up state and notification
    struct state_type* state = malloc(sizeof(struct state_type));
    state->nwarn = nwarn;
    state->warn = warn;
    state->handler_id = (gulong*) NULL;
    state->maxwarn = 0;
    state->last = 100;
    for (i = 0; i < nwarn; i++) {
        if (warn[i] > state->maxwarn) state->maxwarn = warn[i];
    }
    state->closed = malloc(sizeof(char));
    *state->closed = 1;
    notification_init(&state->notification, &state->handler_id, state->closed);

    // block signals we want to handle
    sigset_t sig_set;
    sigemptyset(&sig_set);
    sigaddset(&sig_set, SIGINT);
    sigaddset(&sig_set, SIGTERM);
    sigprocmask(SIG_BLOCK, &sig_set, (sigset_t *) NULL);

    // check battery state now and periodically
    get_bats(state);
    check_bats(state);
    g_timeout_add_seconds(300, (gboolean (*)(gpointer)) &get_bats, state);
    g_timeout_add_seconds(20, (gboolean (*)(gpointer)) &check_bats, state);

    while (1) {
        // check if we have any signals to handle
        if (sigpending(&sig_set) == 0) {
            if (sigismember(&sig_set, SIGINT) ||
                sigismember(&sig_set, SIGTERM)) break;
        }
        g_main_context_iteration((GMainContext*) NULL, FALSE);
        usleep(200000);
    }

    // cleanup
    // frees handler_id if allocated
    notification_uninit(&state->notification, &state->handler_id);
    g_main_context_iteration((GMainContext*) NULL, FALSE);
    free(warn);
    free(state->bat_names);
    free(state->fn);
    free(state->closed);
    free(state);
    return 0;
}
