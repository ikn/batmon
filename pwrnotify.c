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
// if killed, close notification
// recheck for batteries every now and then (ie. don't exit if none found (but still warn))
// (getopt/getopt_long)
// -t --time (seconds between each check)
// -r --reload (seconds between each recheck for batteries)
// -b --background (fork off and die)
// -l --legacy (use /proc/acpi/ instead of /sys/class/)

#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
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

void get_bats (char** bat_names, int* nbats, int* n_max) {
    // get available power supplies
    DIR* dir = opendir("/sys/class/power_supply/");
    if (dir == (DIR*) NULL) {
        *nbats = 0;
        return;
    }
    struct dirent* entry = readdir(dir);
    struct dirent* next;
    int err;
    char buf[7];
    const int mem_blk = 50; // chars to allocate at a time
    int n, nchars = 0, nalloc = 0;
    *bat_names = (char*) NULL;
    *nbats = 0;
    *n_max = 0;
    while (entry != (struct dirent*) NULL) {
        // skip if . or ..
        if (strcmp(entry->d_name, ".") != 0 &&
            strcmp(entry->d_name, "..") != 0) {
            n = strlen(entry->d_name);
            // check supply's type
            char* fn = malloc((24 + n + 5 + 1) * sizeof(char));
            sprintf(fn, "/sys/class/power_supply/%s/type", entry->d_name);
            FILE* f = fopen(fn, "r");
            free(fn);
            if (f != (FILE*) NULL) {
                fread(buf, sizeof(char), 7, f);
                fclose(f);
                // skip if not a battery
                if (strncmp(buf, "Battery", 7) == 0) { // buf has no \0
                    (*nbats)++;
                    while (nchars + n + 1 > nalloc) {
                        // need more room for names
                        nalloc += mem_blk;
                        *bat_names = realloc(*bat_names,
                                             nalloc * sizeof(char));
                    }
                    strcpy(*bat_names + nchars, entry->d_name);
                    nchars += n + 1;
                    if (n > *n_max) *n_max = n;
                }
            }
        }
        err = readdir_r(dir, entry, &next);
        if (err != 0) entry = (struct dirent*) NULL;
        entry = next;
    }
    closedir(dir);
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
    if (qtot == 0) return 0;
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
    gulong id;
    if (*handler_id == (gulong*) NULL) {
        id = g_signal_connect(*notification, "closed",
                              G_CALLBACK(notification_closed), closed);
        *handler_id = &id;
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
    char q = get_charge(state->nbats, state->bat_names, state->fn);
    if (state->last != q) {
        char updated = 0;
        for (int i = 0; i < state->nwarn; i++) {
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
Returns 1 if no batteries could be found, and 2 if invalid arguments are \
given.\n\n", version);
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

    // get batteries
    char* bat_names;
    int nbats, n_max;
    get_bats(&bat_names, &nbats, &n_max);
    if (nbats == 0) {
        fprintf(stderr, "error: no batteries detected\n");
        return 1;
    }

    // periodic check
    struct state_type state;
    state.nwarn = nwarn;
    state.warn = warn;
    state.nbats = nbats;
    state.bat_names = bat_names;
    state.handler_id = (gulong*) NULL;
    state.fn = malloc((24 + n_max + 12 + 1) * sizeof(char));
    state.maxwarn = 0;
    state.last = 100;
    for (i = 0; i < nwarn; i++) {
        if (warn[i] > state.maxwarn) state.maxwarn = warn[i];
    }
    state.closed = malloc(sizeof(char));
    *state.closed = 1;
    notification_init(&state.notification, &state.handler_id, state.closed);

    check_bats(&state);
    g_timeout_add_seconds(20, (gboolean (*)(gpointer)) &check_bats, &state);
    GMainLoop* loop = g_main_loop_new((GMainContext*) NULL, FALSE);
    g_main_loop_run(loop);

    free(state.fn);
    free(state.bat_names);
    free(state.closed);
    return 0;
}
