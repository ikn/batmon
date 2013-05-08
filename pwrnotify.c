/*
 * pwrnotify version 0.2.1
 *
 * a lightweight, standalone battery status notifier
 *
 * Licensed under the GNU General Public License, version 3; if this was not
 * included, you can find it here:
 *     http://www.gnu.org/licenses/gpl-3.0.txt
 */

#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <libnotify/notify.h>

static const char* version = "0.2.1";

// TODO:
// if killed, close notification
// change notification's % when it changes, if visible ("closed" signal, but requires gtk_main())
// recheck for batteries every now and then
// (getopt/getopt_long)
// -t --time (seconds between each check)
// -r --reload (seconds between each recheck for batteries)
// -b --background (fork off and die)
// -l --legacy (use /proc/acpi/ instead of /sys/class/)

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
    *bat_names = NULL;
    *nbats = 0;
    *n_max = 0;
    while (entry != (struct dirent*) NULL) {
        // skip if . or ..
        if (strcmp(entry->d_name, ".") != 0 &&
            strcmp(entry->d_name, "..") != 0) {
            n = strlen(entry->d_name);
            // check supply's type
            char* fn = malloc(24 + n + 5 + 1);
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
                        *bat_names = realloc(*bat_names, nalloc * sizeof(char));
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

int get_charge (int n, char* names, char* fn) {
    // get total battery charge as integer percentage of maximum
    int i, val = 0, q = 0, qtot = 0;
    FILE* f;
    for (i = 0; i < n; i++) {
        // read current charge
        sprintf(fn, "/sys/class/power_supply/%s/charge_now", names);
        f = fopen(fn, "r");
        if (f != (FILE*) NULL) {
            if (fscanf(f, "%d", &val) == 0) val = 0;
            fclose(f);
            q += val;
        }
        // and total charge
        sprintf(fn, "/sys/class/power_supply/%s/charge_full", names);
        f = fopen(fn, "r");
        if (f != (FILE*) NULL) {
            if (fscanf(f, "%d", &val) == 0) val = 0;
            fclose(f);
            qtot += val;
        }
        names += strlen(names);
    }
    if (qtot == 0) return 0;
    else if (q > qtot) return 100;
    else return (q * 100) / qtot;
}

void notification_init (NotifyNotification** notification) {
    notify_init("pwrnotify");
    *notification = notify_notification_new("", "", "");
    notify_notification_set_timeout(*notification, NOTIFY_EXPIRES_NEVER);
}

int notification_show (NotifyNotification* notification, char* body, int q) {
    // should have 0 <= q <= 100, but use snprintf just in case
    snprintf(body, 26, "Battery level is at %d%%.\n", q);
    notify_notification_update(notification, "Low Battery", body,
                               "dialog-warning");
    GError* e = NULL;
    if (!notify_notification_show(notification, &e)) {
        printf("error: %s\n", e->message);
        return 0;
    } else return 1;
}

void notification_uninit (NotifyNotification** notification) {
    notify_notification_close(*notification, (GError**) NULL);
    g_object_unref(G_OBJECT(*notification));
    notify_uninit();
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
    int err, i, j, n,val, nwarn = argc - 1;
    char* warn = calloc(nwarn, sizeof(char)), *arg;
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
    int delay = 20;
    char* fn = malloc(24 + n_max + 12 + 1);
    char body[26], maxwarn = 0, last = 100, q;
    for (i = 0; i < nwarn; i++) {
        if (warn[i] > maxwarn) maxwarn = warn[i];
    }
    NotifyNotification* notification;
    notification_init(&notification);
    while (1) {
        q = get_charge(nbats, bat_names, fn);
        if (last != q) {
            for (i = 0; i < nwarn; i++) {
                if (last >= warn[i] && q < warn[i]) {
                    // dipped below a warning level: show notification
                    if (!notification_show(notification, body, q)) {
                        notification_uninit(&notification);
                        notification_init(&notification);
                        notification_show(notification, body, q);
                    }
                    // already shown: no need to check other warning levels
                    break;
                }
            }
            if (last < maxwarn && q >= maxwarn) {
                // newly above all warning levels: hide notification
                notify_notification_close(notification, (GError**) NULL);
            }
            last = q;
        }
        sleep(delay);
    }

    free(fn);
    free(bat_names);
    return 0;
}
