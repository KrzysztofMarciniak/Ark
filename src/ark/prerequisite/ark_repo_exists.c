#include <stdbool.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define PATH_MAX 4096

static const char* cache_file = ".ark/cache/repo_exists";
static const char* recipes_dir = ".ark/recipes";

static bool check_if_is_directory(const char* path){
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool set_cache(const char* home){
	char path[PATH_MAX];
	FILE* fp;
	if(snprintf(path,sizeof(path), "%s/%s", home, cache_file) >=
		(int)sizeof(path)){
		return false;
	}
	fp = fopen(path, "w");
	if(fp == NULL){
		return false;
	}
	fclose(fp);
	return true;
}

static bool check_cache(const char* home){
	char path[PATH_MAX];
	struct stat st;
	if (snprintf(path, sizeof(path), "%s/%s", home, cache_file) >=
		(int)sizeof(path)){
			return false;
		}
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}


bool ark_check_repo(void){
	const char* home;
	char recipes_path[PATH_MAX];
	char entry_path[PATH_MAX];
	DIR* dir;
	struct dirent* entry;
	
	home = getenv("HOME");
	if (home == NULL){
		return false;
	}
	if(check_cache(home)){
		return true;
	}	
	if(snprintf(recipes_path, sizeof(recipes_path), "%s/%s", home, recipes_dir) >= (int)sizeof(recipes_path)) {
		return false;
	}
	dir = opendir(recipes_path);
	if(dir == NULL){
		return false;
	}
	while ((entry = readdir(dir)) != NULL){
		if(strcmp(entry->d_name, ".") == 0 ||
			strcmp(entry->d_name, "..") == 0){
			continue;
		}
	if (snprintf(entry_path, sizeof(entry_path), "%s/%s", recipes_path, entry->d_name) >= 
			(int)sizeof(entry_path)){
			continue;
		}
	if (check_if_is_directory(entry_path)){
		closedir(dir);
		set_cache(home);
		return true;
	}
	}
	closedir(dir);
	return false;
}
