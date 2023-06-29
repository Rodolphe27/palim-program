#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include  "sem.h"
#include <pthread.h>
#include <sys/stat.h>
#include <string.h>
struct statistics {
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
// TODO: add variables if necessary

// function declarations
static void* processTree(void* path);
static void* processDir(char* path);
static void* processEntry(char* path, struct dirent* entry);
static void* processFile(void* path);
// TODD
//Sephamore
static SEM* statsMutex;
static SEM* newDataSignal;
static SEM* grepThreadsSem;

static char* searchString;

static void usage(void) {
	fprintf(stderr, "Usage: palim <string> <max-grep-threads> <trees...>\n");
	exit(EXIT_FAILURE);
}

static void die(const char *msg) {
	perror(msg);
	exit(EXIT_FAILURE);
}

static void incStat(int*variable){
	P(statsMutex);
	*variable-=1;
	V(statsMutex);
}
static void decStat(int * variable){
	P(statsMutex);
	*variable+=1;
	V(statsMutex);
}

static void printStatistics() {
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
	
	
	if (fflush(stdout)==EOF){
		die("fflush");
	}
    
}


	

/*
 * \brief Initializes necessary data structures and spawns one crawl-Thread per tree.
 * Subsequently, waits passively on statistic updates and on update prints the new statistics.
 * If all threads terminated, it frees all allocated resources and exits/returns.
 */

int main(int argc, char** argv) {
	if(argc < 4) {
		usage();
	}

	// convert argv[2] (<max-grep-threads>) into long with strtol()
	errno = 0;
	char *endptr;
	stats.maxGrepThreads = strtol(argv[2], &endptr, 10);

	// argv[2] can not be converted into long without error
	if(errno != 0 || endptr == argv[2] || *endptr != '\0') {
		usage();
	}

	if(stats.maxGrepThreads <= 0) {
		fprintf(stderr, "max-grep-threads must not be negative or zero\n");
		usage();
	}

	searchString = argv[1];

	statsMutex = semCreate(1);

	if(statsMutex == NULL)
	{
		die("semCreate");
	}

	newDataSignal = semCreate(0);
	if(newDataSignal == NULL)
	{
		die("semCreate");
	}
    grepThreadsSem = semCreate(stats.maxGrepThreads);
	if(grepThreadsSem == NULL)
	{
		die("semCreate");

	}

	pthread_t tids[argc-3];
	for(int i= 3 ; i< argc; i++){
		errno = pthread_create(&tids[i-3], NULL, processTree, argv[i]);
		if(errno !=0 ){
			die("pthread_create");
		}

		incStat(&stats.activeCrawlThreads);

		errno= pthread_detach(tids[i-3]);
		if (errno != 0){
		die("pthread_detach");
		}
	}

	while(1){
		P(statsMutex);
		int crawActiveNr = stats.activeCrawlThreads;
		int grepActiveNr = stats.activeGrepThreads;
		V(statsMutex);

		if(crawActiveNr > 0 || grepActiveNr > 0){
			printStatistics();
		}else{
			break;
		}
	}

	printStatistics();
	if(printf("\n")<0){
		die("printf");
	}
	if(fflush(stdout)== EOF){
		die("fflush");
	}

	semDestroy(statsMutex);
	semDestroy(newDataSignal);
	semDestroy(grepThreadsSem);
	// TODO: implement me!

	return EXIT_SUCCESS;
}

/**
 * \brief Acts as start_routine for crawl-Threads and calls processDir().
 *
 * It updates the stats.activeCrawlThreads field.
 *
 * \param path Path to the directory to process
 *
 * \return Always returns NULL
 */
static void* processTree(void* path) {
	char * treeStr = (char*) path;
	processDir(treeStr);
	decStat(&stats.activeCrawlThreads);
	
	
	
	return NULL;
}

/**
 * \brief Iterates over all directory entries of path and calls processEntry()
 * on each entry (except "." and "..").
 *
 * It updates the stats.dirs field.
 *
 * \param path Path to directory to process
 */

static void* processDir(char* path) {
	DIR * dirPointer = opendir(path);
	if(errno){
		die("opendir");
	}

	incStat(&stats.dirs);
	V(newDataSignal);

	struct dirent* entry = NULL;
	errno=0;

	entry= readdir(dirPointer);

	while(entry!=NULL){
		if(strcmp(entry->d_name,".")!=0 && strcmp(entry->d_name,"..")){
			processEntry(path,entry);
		}
		errno=0;
		entry= readdir(dirPointer);

		
	}
	if(errno){
		die("readdir");
	}
	if(closedir(dirPointer)== -1){
		die("closedir");
	}

	return NULL;
	// TODO: implement me!

}

/**
 * \brief Spawns a new grep-Thread if the entry is a regular file and calls processDir() if the entry
 * is a directory.
 *
 * It updates the stats.activeGrepThreads if necessary. If the maximum number of active grep-Threads is
 * reached the functions waits passively until another grep-Threads can be spawned.
 *
 * \param path Path to the directory of the entry
 * \param entry Pointer to struct dirent as returned by readdir() of the entry
 */
static void* processEntry(char* path, struct dirent* entry) {

	struct stat buf;
	int pathLength= strlen(path);
	int pathPlusEntryLength= pathLength +strlen(entry->d_name) + 2;
	char * pathPlusEntry=malloc(pathPlusEntryLength*sizeof(char));
	if(pathPlusEntry==NULL ){
		die("malloc");

	}

	strcat(pathPlusEntry,path);
	strcat(pathPlusEntry,"/");
	strcat(pathPlusEntry,entry->d_name);

	if(lstat(pathPlusEntry,&buf)== -1){
		free(pathPlusEntry);
		die("lstat");
	}

	if(S_ISDIR(buf.st_mode)){
		processDir(pathPlusEntry);
		printStatistics();
	}

	if(S_ISREG(buf.st_mode)){
		P(grepThreadsSem);

		pthread_t tid;
		errno= pthread_create(&tid,NULL,processFile,pathPlusEntry);
		if(errno != 0){
			die("pthread_create");

		}

		errno = pthread_detach(tid);
		if(errno != 0){
			die("pthread_detach");
		}

		incStat(&stats.activeGrepThreads);
		V(newDataSignal);
	}
	
	printStatistics();

	free(pathPlusEntry);
	return NULL;
	//TODO: implement me!

}

/**
 * \brief Acts as start_routine for grep-Threads and searches all lines of the file for the
 * search pattern.
 *
 * It updates the stats.files, stats.lines, stats.fileHits, stats.lineHits
 * stats.activeGrepThreads fields if necessary.
 *
 * \param path Path to the file to process
 *
 * \return Always returns NULL
 */
static void* processFile(void* path) {

	char* filePath = (char*)path;


	incStat(&stats.files);

	V(newDataSignal);
	
	FILE * fh = fopen(filePath,"r");
	if(!fh){
		die("fopen");
	}

	char line[4096 + 2];

	while(fgets(line, sizeof(line),fh)){
		incStat(&stats.lines);


		if(strstr(line,searchString)){
			incStat(&stats.lineHits);
		}

		V(newDataSignal);
		printStatistics();
	}

	if(ferror(fh))
	{
		die("fgets");
	}

	if(fclose(fh)==EOF){
		die("fclose");
	}

	decStat(&stats.activeGrepThreads);

	V(grepThreadsSem);
	V(newDataSignal);
	//TODO: implement me!

	return NULL;
}

