#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>

#define MAX_PATH_LEN 4096

unsigned long getSize(const char *path) {
    struct stat fstat;

    if (lstat(path, &fstat) == -1) {
        perror("lstat");
        exit(EXIT_FAILURE);
    }

    switch (fstat.st_mode & S_IFMT) {
        case S_IFDIR:
            {
                unsigned long size = 0;
                DIR *dir = opendir(path);
                if (dir == NULL) {
                    perror("opendir");
                    exit(EXIT_FAILURE);
                }

                struct dirent *entry;
                while ((entry = readdir(dir))) {
                    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                        char sub_path[MAX_PATH_LEN];
                        snprintf(sub_path, sizeof(sub_path), "%s/%s", path, entry->d_name);
                        size += getSize(sub_path);
                    }
                }

                closedir(dir);
                return size + fstat.st_size;
            }
            break;
        case S_IFLNK:
            {
                char curr[MAX_PATH_LEN];
                ssize_t len = readlink(path, curr, sizeof(curr) - 1);
                if (len != -1) {
                    curr[len] = '\0';
                    return getSize(curr);
                } else {
                    perror("readlink");
                    exit(EXIT_FAILURE);
                }
            }
            break;
        case S_IFREG:
            {
                return fstat.st_size;
            }
            break;
        default:
            fprintf(stderr, "Unsupported file found. Skipping.\n");
            return 0;
    }

}

int main(int ac, char** av) {
    if (ac != 2) {
        fprintf(stderr, "Usage: %s <relative path to a directory>\n", av[0]);
        exit(EXIT_FAILURE);
    }

    int pfds[2];
    pid_t pid;

    if (pipe(pfds) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    if(pid = fork()) {
        // parent
        if (pid == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        close(pfds[1]);
    
        unsigned long child_tsz;
        read(pfds[0], &child_tsz, sizeof(child_tsz));
        close(pfds[0]);
    
        printf("%lu\n", child_tsz);
    } else {
        // child
        close(pfds[0]);

        unsigned long tsz = getSize(av[1]);
        write(pfds[1], &tsz, sizeof(tsz));
        close(pfds[1]);

        exit(EXIT_SUCCESS);
    }

    return 0;
}
