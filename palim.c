#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include "sem.h"
#include <pthread.h>
#include <sys/stat.h>
#include <string.h>
#include <stdbool.h>

struct statistics
{
    int lines;
    int lineHits;
    int files;
    int fileHits;
    int dirs;
    int activeGrepThreads;
    int maxGrepThreads;
    int activeCrawlThreads;
};

// (module-)global variables
static struct statistics stats;

// function declarations
static void *processTree(void *path);
static void *processDir(char *path);
static void *processEntry(char *path, struct dirent *entry);
static void *processFile(void *path);

// Semaphore variables
static SEM *statsMutex;
static SEM *newDataSignal;
static SEM *grepThreadsSem;

// Search string variable
static char *search_string;

static void usage(void)
{
    fprintf(stderr, "Usage: palim <string> <max-grep-threads> <trees...>\n");
    exit(EXIT_FAILURE);
}

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void incStat(int *variable)
{
    P(statsMutex);
    (*variable)++;
    V(statsMutex);
}

static void decStat(int *variable)
{
    P(statsMutex);
    (*variable)--;
    V(statsMutex);
}

static void printStatistics()
{
    P(statsMutex);
    int lineHits = stats.lineHits;
    int lines = stats.lines;
    int fileHits = stats.fileHits;
    int files = stats.files;
    int dirs = stats.dirs;
    int activeGrepThreads = stats.activeGrepThreads;
    V(statsMutex);

    printf("%d/%d lines, %d/%d files, %d directories, %d active threads\r",
           lineHits, lines, fileHits, files, dirs, activeGrepThreads);

    if (fflush(stdout) == EOF)
    {
        die("fflush");
    }
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        usage();
    }

    // Convert argv[2] (<max-grep-threads>) into long with strtol()
    errno = 0;
    char *endptr;
    stats.maxGrepThreads = strtol(argv[2], &endptr, 10);

    // argv[2] cannot be converted into long without error
    if (errno != 0 || endptr == argv[2] || *endptr != '\0')
    {
        usage();
    }

    if (stats.maxGrepThreads <= 0)
    {
        fprintf(stderr, "max-grep-threads must not be negative or zero\n");
        usage();
    }

    search_string = argv[1];

    statsMutex = semCreate(1);

    if (statsMutex == NULL)
    {
        die("semCreate");
    }

    newDataSignal = semCreate(0);
    if (newDataSignal == NULL)
    {
        die("semCreate");
    }

    grepThreadsSem = semCreate(stats.maxGrepThreads);
    if (grepThreadsSem == NULL)
    {
        die("semCreate");
    }

    pthread_t tids[argc - 3];
    for (int i = 3; i < argc; i++)
    {
        errno = pthread_create(&tids[i - 3], NULL, processTree, argv[i]);
        if (errno != 0)
        {
            die("pthread_create");
        }

        incStat(&stats.activeCrawlThreads);

        errno = pthread_detach(tids[i - 3]);
        if (errno != 0)
        {
            die("pthread_detach");
        }
    }

    while (1)
    {
        P(newDataSignal);
        P(statsMutex);
        int crawlActiveNr = stats.activeCrawlThreads;
        int grepActiveNr = stats.activeGrepThreads;
        V(statsMutex);

        if (crawlActiveNr > 0 || grepActiveNr > 0)
        {
            printStatistics();
        }
        else
        {
            break;
        }
    }

    printStatistics();
    if (printf("\n") < 0)
    {
        die("printf");
    }
    if (fflush(stdout) == EOF)
    {
        die("fflush");
    }

    semDestroy(statsMutex);
    semDestroy(newDataSignal);
    semDestroy(grepThreadsSem);

    return EXIT_SUCCESS;
}

static void *processTree(void *path)
{
    char *treeStr = (char *)path;
    processDir(treeStr);
    decStat(&stats.activeCrawlThreads);
    V(newDataSignal);
    printStatistics();
    return NULL;
}

static void *processDir(char *path)
{
    DIR *dirPointer = opendir(path);
    if (!dirPointer)
    {
        die("opendir");
    }

    incStat(&stats.dirs);
    V(newDataSignal);

    struct dirent *entry = NULL;
    errno = 0;

    entry = readdir(dirPointer);

    while (entry != NULL)
    {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
        {
            processEntry(path, entry);
        }
        errno = 0;
        entry = readdir(dirPointer);
    }
    if (errno)
    {
        die("readdir");
    }
    if (closedir(dirPointer) == -1)
    {
        die("closedir");
    }

    return NULL;
}

static void *processEntry(char *path, struct dirent *entry)
{
    struct stat buf;
    int pathLength = strlen(path);
    int pathPlusEntryLength = pathLength + strlen(entry->d_name) + 2;
    char *pathPlusEntry = malloc(pathPlusEntryLength * sizeof(char));
    if (pathPlusEntry == NULL)
    {
        die("malloc");
    }

    strcpy(pathPlusEntry, path);
    strcat(pathPlusEntry, "/");
    strcat(pathPlusEntry, entry->d_name);

    if (lstat(pathPlusEntry, &buf) == -1)
    {
        free(pathPlusEntry);
        die("lstat");
    }

    if (S_ISDIR(buf.st_mode))
    {
        processDir(pathPlusEntry);
        printStatistics();
    }

    if (S_ISREG(buf.st_mode))
    {
        P(grepThreadsSem);

        pthread_t tid;
        errno = pthread_create(&tid, NULL, processFile, pathPlusEntry);
        if (errno != 0)
        {
            die("pthread_create");
        }

        errno = pthread_detach(tid);
        if (errno != 0)
        {
            die("pthread_detach");
        }

        incStat(&stats.activeGrepThreads);
        V(newDataSignal);
    }

    printStatistics();

    free(pathPlusEntry);
    return NULL;
}

static void *processFile(void *path)
{
    char *filePath = (char *)path;

    incStat(&stats.files);
    V(newDataSignal);

    bool has_hits= false;

    FILE *fh = fopen(filePath, "r");
    if (!fh)
    {
        die("fopen");
    }

    char line[4096 + 2];

    while (fgets(line, sizeof(line), fh))
    {
        incStat(&stats.lines);
        V(newDataSignal);

        if (strstr(line, search_string))
        {
            incStat(&stats.lineHits);
            has_hits= true;
            V(newDataSignal);
        }

        printStatistics();
    }

    if (has_hits){
        incStat(&stats.fileHits);
        V(newDataSignal);
    }

    if (ferror(fh))
    {
        die("fgets");
    }

    if (fclose(fh) != 0)
    {
        die("fclose");
    }

    decStat(&stats.activeGrepThreads);
    V(newDataSignal);
    printStatistics();

    return NULL;
}
