/*
 * pwrnotify version 0.1
 *
 * a lightweight, standalone battery status notifier
 *
 * Licensed under the GNU General Public License, version 3; if this was not
 * included, you can find it here:
 *     http://www.gnu.org/licenses/gpl-3.0.txt
 */

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <libnotify/notify.h>

// TODO:
// (getopt/getopt_long)
// args are comma-separated warn levels as integer percentages
// -t --time (seconds between each check)
// -r --reload (seconds between each recheck for batteries)
// -b --background (fork off and die)
// -l --legacy (use /proc/acpi/ instead of /sys/class/)

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

int main (int argc, char** argv) {
    // get available power supplies
    DIR* dir = opendir("/sys/class/power_supply/");
    if (dir == (DIR*) NULL) {
        fprintf(stderr, "error: no power supplies detected\n");
        return 1;
    }
    struct dirent* entry = readdir(dir);
    struct dirent* next;
    int err;
    char buf[7];
    const int mem_blk = 50; // chars to allocate at a time
    char* bat_names = (char*) NULL;
    int n, n_max = 0, nbats = 0, nchars = 0, nalloc = 0;
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
                    nbats++;
                    while (nchars + n + 1 > nalloc) {
                        // need more room for names
                        nalloc += mem_blk;
                        bat_names = realloc(bat_names, nalloc * sizeof(char));
                    }
                    strcpy(bat_names + nchars, entry->d_name);
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
    if (nbats == 0) {
        fprintf(stderr, "error: no batteries detected\n");
        return 1;
    }

    // periodic check
    int nwarn = 3, warn[3] = {3, 5, 10};
    int delay = 20;
    char* fn = malloc(24 + n_max + 12 + 1);
    char body[26];
    int i, last = 100, q;
    notify_init ("pwrnotify");
    NotifyNotification* notification = notify_notification_new("", "", "");
    notify_notification_set_timeout(notification, NOTIFY_EXPIRES_NEVER);
    while (1) {
        q = get_charge(nbats, bat_names, fn);
        if (last != q) {
            for (i = 0; i < nwarn; i++) {
                if (last >= warn[i] && q < warn[i]) {
                    // dipped below a warning level: show notification
                    // should have 0 <= q <= 100, but use snprintf just in case
                    snprintf(body, 26, "Battery level is at %d%%.\n", q);
                    notify_notification_update(notification, "Low Battery",
                                               body, "dialog-warning");
                    notify_notification_show(notification, (GError**) NULL);
                    // already shown: no need to check other warning levels
                    break;
                }
            }
            last = q;
        }
        sleep(delay);
    }

    free(fn);
    free(bat_names);
    return 0;
}
