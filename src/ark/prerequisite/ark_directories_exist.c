#include "ark_directories_exist.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdbool.h>

static const char* directories_needed[] = {".ark",".ark/bin", ".ark/recipes", ".ark/recipes/cache", NULL};

static int ensure_directories(const char* path) {
        struct stat st;
        if (stat(path, &st) == 0) {
                return S_ISDIR(st.st_mode) ? 0 : -1;
        }
        if (errno != ENOENT) {
                return -1;
        }
	if(mkdir(path,0755) == -1 && errno != EEXIST){
		return -1;
	}
	return 0;
}

bool ark_check_directory_existance(void) {
	const char* home = getenv("HOME");
	char path[4096];
	if (home == NULL){
		return true;
	}
	for (size_t i = 0; directories_needed[i] != NULL; ++i){
		if(snprintf(path, sizeof(path), "%s/%s",
				home, directories_needed[i]) >=
					(int)sizeof(path)){
						return true;
					}
	if(ensure_directories(path) != 0){
			perror(path);
			return true;
		}	
	}
	return false;
}
