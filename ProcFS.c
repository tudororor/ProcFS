#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>

int is_numeric(const char *str) {
    while (*str) {
        if (!isdigit(*str)) return 0;
        str++;
    }
    return 1;
}

// Obține PID-ul procesului curent din calea FUSE
// Exemplu: /1/123 -> returnează 123. / -> returnează 0
pid_t get_pid_from_path(const char *path) {
    if (strcmp(path, "/") == 0) 
        return 0;

    const char *last_slash = strrchr(path, '/');
    if (!last_slash) 
        return -1;

    // Verificăm dacă fișierul este "stats"
    if (strcmp(last_slash + 1, "stats") == 0) 
    {
        // Trebuie să luăm PID-ul directorului părinte
        // Copiem calea pentru a o modifica
        char temp_path[256];
        strncpy(temp_path, path, 256);
        temp_path[255] = '\0';
        
        // Eliminăm "/stats"
        char *slash = strrchr(temp_path, '/');
        if (slash) 
            *slash = '\0';
        
        // Luăm noul ultim element (PID-ul)
        slash = strrchr(temp_path, '/');
        if (!slash) 
            return 0; // Nu ar trebui să se întâmple dacă structura e corectă
        return atoi(slash + 1);
    }

    return atoi(last_slash + 1);
}

// Citește PPID-ul (Parent PID) real din /proc/[pid]/stat
pid_t get_real_ppid(pid_t pid) {
    char stat_path[256];
    snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);

    FILE *f = fopen(stat_path, "r");
    if (!f) return -1;

    // Formatul /proc/[pid]/stat este complex, numele procesului poate conține spații și paranteze.
    // PPID este al 4-lea câmp. Trebuie să sărim peste (comm).
    int read_pid;
    char state;
    int ppid;

    // Citim PID
    if (fscanf(f, "%d", &read_pid) != 1) { fclose(f); return -1; }
    
    // Sărim peste 'comm' care este între paranteze (poate conține spații)
    int c;
    while ((c = fgetc(f)) != '(' && c != EOF);
    while ((c = fgetc(f)) != ')' && c != EOF);
    
    // Citim starea și PPID
    if (fscanf(f, " %c %d", &state, &ppid) != 2) { fclose(f); return -1; }

    fclose(f);
    return ppid;
}

// Funcția getattr - Returnează atributele fișierului/directorului
static int do_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));

    // Cazul Rădăcină
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    // Verificăm dacă este fișierul "stats"
    const char *filename = strrchr(path, '/') + 1;
    if (strcmp(filename, "stats") == 0) {
        stbuf->st_mode = S_IFREG | 0444; // Read-only
        stbuf->st_nlink = 1;
        stbuf->st_size = 1024; // Dimensiune fictivă, sau calculată real
        return 0;
    }

    // Verificăm dacă este un director PID valid
    if (is_numeric(filename)) {
        pid_t pid = atoi(filename);
        // Verificăm dacă procesul există real în /proc
        if (get_real_ppid(pid) != -1) {
            stbuf->st_mode = S_IFDIR | 0755;
            stbuf->st_nlink = 2;
            return 0;
        }
    }

    return -ENOENT;
}

// Funcția readdir - Listează conținutul directorului
static int do_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi) {
    (void) offset;
    (void) fi;

    pid_t current_pid = get_pid_from_path(path);

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    // Dacă nu suntem în rădăcină, adăugăm fișierul stats
    if (strcmp(path, "/") != 0) {
        filler(buf, "stats", NULL, 0);
    }

    // Scanăm /proc pentru a găsi copiii procesului curent
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) return -errno;

    struct dirent *entry;
    while ((entry = readdir(proc_dir)) != NULL) {
        // Ne interesează doar directoarele numerice
        if (!is_numeric(entry->d_name)) continue;

        pid_t pid = atoi(entry->d_name);
        pid_t ppid = get_real_ppid(pid);

        // Dacă PPID-ul procesului găsit este egal cu PID-ul directorului curent
        // Înseamnă că este un copil în structura noastră arborescentă
        if (ppid == current_pid) {
            filler(buf, entry->d_name, NULL, 0);
        }
    }

    closedir(proc_dir);
    return 0;
}

// Funcția read - Citește conținutul fișierului stats
static int do_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {
    (void) fi;

    // Verificăm dacă se citește fișierul stats
    const char *filename = strrchr(path, '/') + 1;
    if (strcmp(filename, "stats") != 0) return -ENOENT;

    pid_t pid = get_pid_from_path(path);
    
    // Citim informații reale din /proc
    char stat_path[256];
    snprintf(stat_path, sizeof(stat_path), "/proc/%d/status", pid);

    FILE *f = fopen(stat_path, "r");
    if (!f) return -EIO;

    // Citim conținutul într-un buffer local
    char content[2048];
    size_t len = fread(content, 1, sizeof(content) - 1, f);
    content[len] = '\0';
    fclose(f);

    // Copiem în buffer-ul FUSE
    size_t len_content = strlen(content);
    if (offset < len_content) {
        if (offset + size > len_content)
            size = len_content - offset;
        memcpy(buf, content + offset, size);
    } else {
        size = 0;
    }

    return size;
}

static struct fuse_operations proc_oper = {
    .getattr    = do_getattr,
    .readdir    = do_readdir,
    .read       = do_read,
};

int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &proc_oper, NULL);
}
