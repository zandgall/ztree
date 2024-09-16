#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>

struct fileentry {
	struct dirent* dirent;
	char* path;
};

struct direntry {
	char* name, *path;
	int n_dirs;
	struct direntry *dirs;
	int n_files;
	struct fileentry *files;
};

const char* s_this_dir = ".";
const char* s_parent_dir = "..";
const char* s_space = "   ";
const char* s_pipe = "│  ";
const char* s_entry = "├─";
const char* s_lastentry = "└─";
char* prep;

bool hidden = false, logging = false;

const char *asciiPre = "\33[", asciiPost = 'm';

int n_lscolors = 0;
char **lscolor_keys, **lscolor_values;

// Find the LS COLOR given the key
char* getLSColor(char* key) {
	for(int i = 0; i < n_lscolors; i++)
		if(strcmp(lscolor_keys[i], key)==0)
			return lscolor_values[i];
	return "";
}

// Check extension of filename to find an LS COLOR for it
char* matchFileLSColors(char* filename) {
	int extensionLen = 0, extensionStart = 0;
	// Search for last '.' character to mark beginning and length of extension
	for(int i = 0, n = strlen(filename); i < n; i++)
		if(filename[i]=='.') {
			extensionLen = 1;
			extensionStart = i;
		} else
			extensionLen++;
	// Save extension from filename given start and length
	char* extension = malloc(extensionLen+1);
	memcpy(extension, filename+extensionStart, extensionLen);
	extension[extensionLen] = 0;

	// Find color keys starting with '*' (matches filename) and check if extension matches
	for(int i = 0; i < n_lscolors; i++)
		if(lscolor_keys[i][0] == '*' && strcmp(lscolor_keys[i]+1, extension)==0)
			return lscolor_values[i];

	// Default to empty string
	return "";
}

char* directoryColor, *fileColor, *normalColor, *resetColor;

void fillentry(struct direntry* entry);
void printdir(struct direntry dir);

int main(int argc, char** args) {
	char cwd[PATH_MAX];
	if (getcwd(cwd, sizeof(cwd)) == NULL)
		return 1;

	int opt;
	while((opt = getopt(argc, args, "al")) != -1) {
		switch(opt) {
			case 'a':
				hidden = true;
				break;
			case 'l':
				logging = true;
				break;
		}
	}

	printf("Analyzing working dir \"%s\"\n", cwd);
	struct direntry root = (struct direntry){
		cwd, cwd,
		0, NULL,
		0, NULL
	};

	fillentry(&root);

	prep = malloc(PATH_MAX);
	memset(prep, 0, PATH_MAX);

	/// Parse LS_COLORS environment variable for color coding
	char* lscolors = getenv("LS_COLORS");

	// allocate PATH_MAX possible keys and values
	lscolor_keys = (char**)malloc(PATH_MAX*sizeof(char*));
	lscolor_values = (char**)malloc(PATH_MAX*sizeof(char*));
	// lens[2] will be used to determine key length [0] and value length [1]
	// lentype will be used to switch between key length [0] and value length [1]
	// checkpoint is used to keep the beginning of the current key-value pair
	int lentype = 0, lens[2] = {0, 0}, checkpoint = 0;
	for(int i = 0, n = strlen(lscolors); i < n; i++) {
		switch(lscolors[i]) {
			case ':': // End of lscolor key-value pair
				// Allocate and save key
				lscolor_keys[n_lscolors] = malloc(lens[0] + 1);
				memcpy(lscolor_keys[n_lscolors], lscolors + checkpoint, lens[0]);
				lscolor_keys[n_lscolors][lens[0]] = 0;

				// Allocate and save value with additional '\33[' and 'm' ANSI controls
				lscolor_values[n_lscolors] = malloc(lens[1] + 1 + strlen(asciiPre));
				memcpy(lscolor_values[n_lscolors], asciiPre, strlen(asciiPre));
				memcpy(lscolor_values[n_lscolors]+strlen(asciiPre), lscolors + checkpoint + lens[0] + 1, lens[1]);
				lscolor_values[n_lscolors][lens[1] + strlen(asciiPre)] = asciiPost;
				lscolor_values[n_lscolors][lens[1] + strlen(asciiPre)+1] = 0;

				// Move to next color, set checkpoint, and reset lens + lentype
				n_lscolors++;
				checkpoint = i+1;
				lentype = 0;
				lens[0] = 0;
				lens[1] = 0;
				break;
			case '=': // Switch to value type
				lentype = 1;
				break;
			default: // Any non-controlling character adds to selected length
				lens[lentype]++;
		}
	}


	directoryColor = getLSColor("di");
	normalColor = getLSColor("no");
	fileColor = getLSColor("fi");
	resetColor = getLSColor("rs");

	printdir(root);

	return 0;
}

void fillentry(struct direntry* entry) {
	// Allocate PATH_MAX directory and file entries
	struct direntry* dirents = (struct direntry*)malloc(PATH_MAX * sizeof(struct direntry));
	struct fileentry* fileents = (struct fileentry*)malloc(PATH_MAX * sizeof(struct fileentry));

	// Open the directory and analyze every entry
	struct dirent* dp;
	DIR* dir = opendir(entry->path);
	if(logging) {
		printf("Checking directory \"%s\"\n", entry->path);
	}
	while (dir!=NULL && (dp = readdir(dir)) != NULL) {
		// Ignore hidden files depending on -a flag
		if(!hidden && dp->d_name[0] == '.')
			continue;

		// Save the concurrent path of this file
		char *path = malloc(strlen(entry->path) + strlen(dp->d_name) + 2);
		sprintf(path, "%s/%s", entry->path, dp->d_name);

		switch(dp->d_type) {
			// Add directory, if it isn't "." or ".."
			case DT_DIR:
				if(strcmp(dp->d_name, s_this_dir)==0||strcmp(dp->d_name, s_parent_dir)==0)
					continue;

				if(logging)
					printf("Adding directory \"%s\"\n", dp->d_name);
				dirents[entry->n_dirs++] = (struct direntry) {
					dp->d_name, path,
					0, NULL,
					0, NULL
				};
				break;
			// Add all other types as a file
			default:
				if(logging)
					printf("Adding file \"%s\"\n", dp->d_name);
				fileents[entry->n_files++] = (struct fileentry){dp, path};
				break;
		}
	}

	// Truncate entries collected to actual amount, and free temp list
	entry->dirs = (struct direntry*)malloc(entry->n_dirs*sizeof(struct direntry));
	for(int i = 0; i < entry->n_dirs; i++)
		entry->dirs[i] = dirents[i];
	free(dirents);

	entry->files = (struct fileentry*)malloc(entry->n_files*sizeof(struct fileentry));
	for(int i = 0; i < entry->n_files; i++)
		entry->files[i] = fileents[i];
	free(fileents);

	// Fill contents of subdirectories
	for(int i = 0; i < entry->n_dirs; i++)
		fillentry(entry->dirs + i);
}

void printfile(struct fileentry file);

void printdir(struct direntry dir) {
	// Print the directory name
	printf("%s%s%s\n", directoryColor, dir.name, resetColor);

	// Loop through every directory and find out how to print it
	for (int i = 0; i < dir.n_dirs; i++) {

		// Find out whether to use "├" or "└" character, if it's the last entry
		const char* prepend;
		if(i == dir.n_dirs - 1 && dir.n_files == 0)
			prepend = s_lastentry;
		else
			prepend = s_entry;
		printf("%s%s ", prep, prepend); // Prep is any pipes "│" to denote parent folders with more content

		// While the selected directory has only one entry, we will print its name and check its entry
		// If the entry is a file, print that and move to the next directory of "dir"
		// If the entry is a directory, select it and do the same check
		struct direntry *next_dir = dir.dirs+i;
		while(next_dir->n_dirs + next_dir->n_files == 1) {
			printf("%s%s%s/", directoryColor, next_dir->name, resetColor);
			if(next_dir->n_files == 1) {
				printfile(next_dir->files[0]);
				break;
			} else {
				next_dir = next_dir->dirs;
			}
		}

		// If our directory is not one that holds a single file, (would have been printed already) print it
		if(next_dir->n_files != 1 || next_dir->n_dirs != 0) {
			// Save current length of our prep var to truncate it later
			int end = strlen(prep);
			if(i == dir.n_dirs - 1 && dir.n_files == 0)
				strcat(prep, s_space);
			else
				strcat(prep, s_pipe);

			printdir(*next_dir);
			prep[end] = 0;
		}
	}

	// Loop through files and print them
	for (int i = 0; i < dir.n_files; i++) {
		// Find out whether to use "├" or "└" character, if it's the last entry
		const char* prepend;
		if(i == dir.n_files - 1)
			prepend = s_lastentry;
		else
			prepend = s_entry;
		printf("%s%s ", prep, prepend);
		printfile(dir.files[i]);
	}
}

void printfile(struct fileentry file) {
	const char* color;

	// Check file type and stats to determine color
	switch(file.dirent->d_type) {
		case DT_REG:
			// If the file matches a color-coded file extension, use it
			char* fileMatchColor = matchFileLSColors(file.dirent->d_name);
			if(strlen(fileMatchColor)!=0) {
				color = fileMatchColor;
				break;
			}
			
			// Otherwise, if it's executable:
			struct stat sb;
			if(stat(file.path, &sb) == 0) {
				if(sb.st_mode & S_IXUSR) {
					color = getLSColor("ex");
					break;
				}
			}

			// Default to fileColor
			color = fileColor;
			break;
		case DT_LNK: // Sym link
			color = getLSColor("ln");
			break;
		case DT_FIFO: // Pipe
			color = getLSColor("pi");
			break;
		case DT_SOCK: // Socket
			color = getLSColor("so");
			break;
		case DT_BLK: // Block device
			color = getLSColor("bd");
			break;
		case DT_CHR: // Character Device
			color = getLSColor("cd");
			break;
		case DT_UNKNOWN:
		default:
			color = normalColor;
	}
	printf("%s%s%s\n", color, file.dirent->d_name, resetColor);
}
