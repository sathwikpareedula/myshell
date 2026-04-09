/* myshell.c
 * Name: Sathwik pareedula
 * Course: CSE 3320-004
 * Lab 1
 *
 * Build:
 *   gcc -std=c11 -Wall -Wextra -o myshell myshell.c
 * Run: ./myshell [start-directory]
 * References:
  man7.org for Linux system calls (getcwd, fork, exec, waitpid, etc.)
  Class lecture notes for directory traversal logic
  StackOverflow for example usage of strtok_r in parsing input
 
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/wait.h>
#include <ctype.h>

#define MAX_ENTRIES 1024
#define MAX_NAME_LEN 2048
#define DEFAULT_WINDOW_SIZE 5

typedef struct {
    char *name;             // malloc'd
    char fullpath[PATH_MAX];
    off_t size;
    time_t mtime;
    mode_t mode;
    int is_dir;
} Entry;

static Entry files[MAX_ENTRIES];
static Entry dirs[MAX_ENTRIES];
static int n_files = 0, n_dirs = 0;
static int window_start = 0;
static int window_size = DEFAULT_WINDOW_SIZE;

// Helper: trim newline
static void trim_newline(char *s) {
    if (!s) return;
    char *p = strchr(s, '\n');
    if (p) *p = '\0';
}

// Clear memory from previous load
static void clear_entries(void) {
    for (int i = 0; i < n_files; ++i) {
        free(files[i].name);
        files[i].name = NULL;
    }
    for (int i = 0; i < n_dirs; ++i) {
        free(dirs[i].name);
        dirs[i].name = NULL;
    }
    n_files = n_dirs = 0;
    window_start = 0;
}

// Load current directory (single pass)
static int load_directory(void) {
    clear_entries();
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        perror("getcwd");
        return -1;
    }

    DIR *d = opendir(".");
    if (!d) {
        perror("opendir");
        return -1;
    }

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        // skip over nothing — we include "." and ".." as directories (assignment sample shows "..")
        if (n_files + n_dirs >= MAX_ENTRIES) {
            fprintf(stderr, "Warning: max entries reached (%d). Some entries ignored.\n", MAX_ENTRIES);
            break;
        }

        char full[PATH_MAX];
        if (snprintf(full, sizeof(full), "%s/%s", cwd, de->d_name) >= (int)sizeof(full)) {
            // name too long, skip
            continue;
        }

        struct stat st;
        if (stat(full, &st) == -1) {
            // cannot stat -> skip but warn
            // (don't fatal: some files may be removed or inaccessible)
            // perror("stat");
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            dirs[n_dirs].name = strdup(de->d_name);
            strncpy(dirs[n_dirs].fullpath, full, PATH_MAX-1);
            dirs[n_dirs].fullpath[PATH_MAX-1] = '\0';
            dirs[n_dirs].size = st.st_size;
            dirs[n_dirs].mtime = st.st_mtime;
            dirs[n_dirs].mode = st.st_mode;
            dirs[n_dirs].is_dir = 1;
            n_dirs++;
        } else {
            files[n_files].name = strdup(de->d_name);
            strncpy(files[n_files].fullpath, full, PATH_MAX-1);
            files[n_files].fullpath[PATH_MAX-1] = '\0';
            files[n_files].size = st.st_size;
            files[n_files].mtime = st.st_mtime;
            files[n_files].mode = st.st_mode;
            files[n_files].is_dir = 0;
            n_files++;
        }
    }

    closedir(d);
    return 0;
}

static void print_time_and_cwd(void) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) strncpy(cwd, "unknown", sizeof(cwd));

    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    char timestr[128];
    if (lt) strftime(timestr, sizeof(timestr), "%d %B %Y, %I:%M %p", lt);
    else strcpy(timestr, "unknown time");

    printf("Current Working Dir: %s\n", cwd);
    printf("It is now: %s\n\n", timestr);
}

static void display_listing(void) {
    print_time_and_cwd();
    printf("Files (showing %d items per page):\n", window_size);

    if (n_files == 0) {
        printf("  (no files)\n");
    } else {
        int end = window_start + window_size;
        if (end > n_files) end = n_files;
        for (int i = window_start; i < end; ++i) {
            // print index in full file list
            struct tm *mt = localtime(&files[i].mtime);
            char timestr[32] = "";
            if (mt) strftime(timestr, sizeof(timestr), "%b %d %H:%M", mt);
            printf("%3d. %-40s  %8lld bytes  %s\n", i, files[i].name, (long long)files[i].size, timestr);

        }
    }

    printf("\nDirectories:\n");
    if (n_dirs == 0) {
        printf("  (no directories)\n");
    } else {
        for (int i = 0; i < n_dirs; ++i) {
            printf("%3d. %s\n", i, dirs[i].name);
        }
    }

    printf("\nOperations:\n");
    printf(" D - Display/Refresh\n");
    printf(" E - Edit file\n");
    printf(" R - Run executable file (can give parameters)\n");
    printf(" C - Change directory (or M - Move to directory)\n");
    printf(" S - Sort files (size or date)\n");
    printf(" N - Next page of files\n");
    printf(" P - Previous page of files\n");
    printf(" X - Remove (delete) file\n");
    printf(" Q - Quit\n");
    printf("\nEnter command: ");
    fflush(stdout);
}

// Find file index by numeric string or prefix; returns index or -1
static int find_file_by_input(const char *input) {
    if (!input || input[0] == '\0') return -1;
    // try number
    int i = 0;
    while (input[i] && isspace((unsigned char)input[i])) i++;
    if (isdigit((unsigned char)input[i])) {
        long idx = strtol(input + i, NULL, 10);
        if (idx >= 0 && idx < n_files) return (int)idx;
        return -1;
    }
    // else prefix match (case sensitive)
    size_t L = strlen(input);
    for (int j = 0; j < n_files; ++j) {
        if (strncmp(files[j].name, input, L) == 0) return j;
    }
    return -1;
}

static int find_dir_by_input(const char *input) {
    if (!input || input[0] == '\0') return -1;
    int i = 0;
    while (input[i] && isspace((unsigned char)input[i])) i++;
    if (isdigit((unsigned char)input[i])) {
        long idx = strtol(input + i, NULL, 10);
        if (idx >= 0 && idx < n_dirs) return (int)idx;
        return -1;
    }
    size_t L = strlen(input);
    for (int j = 0; j < n_dirs; ++j) {
        if (strncmp(dirs[j].name, input, L) == 0) return j;
    }
    return -1;
}

static void run_editor_on_file(int idx) {
    if (idx < 0 || idx >= n_files) {
        printf("Invalid file index\n");
        return;
    }
    char *editor = getenv("EDITOR");
    if (!editor) editor = "nano"; // fallback
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return;
    } else if (pid == 0) {
        char *argv[4];
        argv[0] = editor;
        argv[1] = files[idx].fullpath;
        argv[2] = NULL;
        execvp(editor, argv);
        // if execvp fails:
        perror("execvp(editor)");
        _exit(127);
    } else {
        int status;
        waitpid(pid, &status, 0);
    }
}

static void run_program(int idx) {
    if (idx < 0 || idx >= n_files) {
        printf("Invalid file index\n");
        return;
    }
    // check executable
    if (access(files[idx].fullpath, X_OK) != 0) {
        printf("File is not executable or not accessible: %s\n", strerror(errno));
        // allow scripts that expect interpreter? we still attempt if readable
        // we will ask user to provide a run command or return
        printf("You can run this file with an interpreter by giving a command, or make it executable.\n");
        return;
    }

    printf("Enter parameters (space separated), or press Enter for none:\n> ");
    char line[4096];
    if (!fgets(line, sizeof(line), stdin)) { clearerr(stdin); line[0] = '\0'; }
    trim_newline(line);

    // build argv: argv[0] = filename (or basename), then tokens
    char *argv[64];
    int a = 0;
    argv[a++] = files[idx].fullpath; // argv[0] can be fullpath
    char *tok = NULL;
    char *saveptr = NULL;
    if (line[0] != '\0') {
        tok = strtok_r(line, " \t", &saveptr);
        while (tok && a < (int)(sizeof(argv)/sizeof(argv[0]) - 1)) {
            argv[a++] = tok;
            tok = strtok_r(NULL, " \t", &saveptr);
        }
    }
    argv[a] = NULL;

    pid_t pid = fork();
    if (pid == -1) { perror("fork"); return; }
    else if (pid == 0) {
        execv(files[idx].fullpath, argv);
        perror("execv");
        _exit(127);
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            printf("Program exited with status %d\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("Program killed by signal %d\n", WTERMSIG(status));
        }
    }
}

static void change_directory_by_index(int id) {
    if (id < 0 || id >= n_dirs) { printf("Invalid directory index\n"); return; }
    if (chdir(dirs[id].fullpath) != 0) {
        perror("chdir");
        return;
    }
    // reload directory
    if (load_directory() != 0) {
        fprintf(stderr, "Warning: could not reload directory after chdir\n");
    }
}

static int cmp_size_desc(const void *a, const void *b) {
    const Entry *A = a;
    const Entry *B = b;
    if (A->size < B->size) return 1;
    else if (A->size > B->size) return -1;
    return 0;
}
static int cmp_mtime_desc(const void *a, const void *b) {
    const Entry *A = a;
    const Entry *B = b;
    if (A->mtime < B->mtime) return 1;
    else if (A->mtime > B->mtime) return -1;
    return 0;
}

static void sort_files_by_size(void) {
    qsort(files, n_files, sizeof(Entry), cmp_size_desc);
}
static void sort_files_by_date(void) {
    qsort(files, n_files, sizeof(Entry), cmp_mtime_desc);
}

static void remove_file_by_index(int idx) {
    if (idx < 0 || idx >= n_files) { printf("Invalid file index\n"); return; }
    printf("Are you sure you want to remove %s ? (y/N): ", files[idx].name);
    fflush(stdout);
    char buf[8];
    if (!fgets(buf, sizeof(buf), stdin)) { clearerr(stdin); return; }
    if (buf[0] != 'y' && buf[0] != 'Y') {
        printf("Aborted.\n");
        return;
    }
    if (unlink(files[idx].fullpath) != 0) {
        perror("unlink");
        return;
    }
    printf("Removed %s\n", files[idx].name);
    // reload directory
    if (load_directory() != 0) fprintf(stderr, "Warning: could not reload directory after remove\n");
}

int main(int argc, char *argv[]) {
    if (argc > 1) {
        // try to chdir to first argument
        if (chdir(argv[1]) != 0) {
            perror("chdir(start dir)");
            // continue in current dir (but warn)
        }
    }

    if (load_directory() != 0) {
        fprintf(stderr, "Failed to load directory. Exiting.\n");
        return 1;
    }

    char cmdline[256];
    while (1) {
        display_listing();
        if (!fgets(cmdline, sizeof(cmdline), stdin)) {
            clearerr(stdin);
            continue;
        }
        trim_newline(cmdline);
        // find first non-space char
        char *p = cmdline;
        while (*p && isspace((unsigned char)*p)) p++;
        char cmd = toupper((unsigned char)*p);

        if (!cmd) {
            // empty input -> continue
            continue;
        }

        if (cmd == 'Q') {
            printf("Quitting.\n");
            break;
        } else if (cmd == 'D') {
            // just refresh -> reload listing
            if (load_directory() != 0) fprintf(stderr, "Warning: reload failed\n");
        } else if (cmd == 'N') {
            if (n_files == 0) { printf("No files.\n"); continue; }
            if (window_start + window_size < n_files) {
                window_start += window_size;
                if (window_start >= n_files) window_start = n_files - 1;
            } else {
                printf("Already at end.\n");
            }
        } else if (cmd == 'P') {
            if (n_files == 0) { printf("No files.\n"); continue; }
            if (window_start - window_size >= 0) {
                window_start -= window_size;
            } else {
                window_start = 0;
            }
        } else if (cmd == 'E') {
            printf("Edit which file? (number or prefix): ");
            char sel[256];
            if (!fgets(sel, sizeof(sel), stdin)) { clearerr(stdin); continue; }
            trim_newline(sel);
            int idx = find_file_by_input(sel);
            if (idx < 0) { printf("File not found.\n"); continue; }
            run_editor_on_file(idx);
            // reload listing (file may have changed)
            load_directory();
        } else if (cmd == 'R') {
            printf("Run which file? (number or prefix): ");
            char sel[256];
            if (!fgets(sel, sizeof(sel), stdin)) { clearerr(stdin); continue; }
            trim_newline(sel);
            int idx = find_file_by_input(sel);
            if (idx < 0) { printf("File not found.\n"); continue; }
            run_program(idx);
        } else if (cmd == 'C' || cmd == 'M') {
            printf("Change to which directory? (number or prefix or path): ");
            char sel[PATH_MAX];
            if (!fgets(sel, sizeof(sel), stdin)) { clearerr(stdin); continue; }
            trim_newline(sel);
            // if looks like an absolute/relative path not number/prefix, try chdir directly
            if (sel[0] == '/' || strchr(sel, '/')) {
                if (chdir(sel) != 0) perror("chdir");
                else load_directory();
            } else {
                int d = find_dir_by_input(sel);
                if (d < 0) {
                    printf("Directory not found.\n");
                } else {
                    change_directory_by_index(d);
                }
            }
        } else if (cmd == 'S') {
            printf("Sort by (s)ize or (d)ate ? ");
            char tmp[16];
            if (!fgets(tmp, sizeof(tmp), stdin)) { clearerr(stdin); continue; }
            trim_newline(tmp);
            if (tmp[0] == 's' || tmp[0] == 'S') sort_files_by_size();
            else sort_files_by_date();
        } else if (cmd == 'X') {
            printf("Remove which file? (number or prefix): ");
            char sel[256];
            if (!fgets(sel, sizeof(sel), stdin)) { clearerr(stdin); continue; }
            trim_newline(sel);
            int idx = find_file_by_input(sel);
            if (idx < 0) { printf("File not found.\n"); continue; }
            remove_file_by_index(idx);
        } else {
            printf("Unknown command '%c'. Try again.\n", cmd);
        }
    }

    clear_entries();
    return 0;
}
