/* Copyright (C) 2020 fosslinux
 * This file is part of mescc-tools
 *
 * mescc-tools is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mescc-tools is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mescc-tools.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "M2libc/bootstrappable.h"

#define MAX_STRING 4096
#define MAX_ARRAY 256
#define COPY_BUFFER_SIZE 262144

/* Globals */
int verbose;

/* UTILITY FUNCTIONS */

/* Function to find a character's position in a string (last match) */
int find_last_char_pos(char* string, char a)
{
	int i = strlen(string) - 1;
	if(i < 0) return i;
	while(i >= 0)
	{
		/*
		 * This conditional should be in the while conditional but we are
		 * running into the M2-Planet short-circuit bug.
		 */
		if(a == string[i]) break;
		i = i - 1;
	}
	return i;
}

/* PROCESSING FUNCTIONS */

char* directory_dest(char* dest, char* source, int require_directory)
{
	/*
	 * First, check if it is a directory to copy to.
	 * We have two ways of knowing this:
	 * - If the destination ends in a slash, the user has explicitly said
	 *   it is a directory.
	 * - Normally we would use stat() but we don't want to force support for
	 *   that syscall onto the kernel, so we just attempt to chdir() into it
	 *   and if it works then it must be a directory. A bit hacky, bit it
	 *   works.
	 */
	int isdirectory = FALSE;
	int dest_len = strlen(dest);
	require(0 < dest_len, "Provide a destination file\n");
	if(match(dest, "."))
	{
		isdirectory = TRUE;
	}
	if(dest[dest_len - 1] == '/')
	{
		isdirectory = TRUE;
	}
	if(!isdirectory)
	{ /* Use the other testing method */
		/*
		 * Get the current path so that we can chdir back to it if it does
		 * chdir successfully.
		 */
		char* current_path = calloc(MAX_STRING, sizeof(char));
		require(current_path != NULL, "Memory initialization of current_path in directory_dest failed\n");
		getcwd(current_path, MAX_STRING);
		require(!match("", current_path), "getcwd() failed\n");
		/*
		 * chdir expects an absolute path.
		 * If the first character is / then it is already absolute, otherwise
		 * it is relative and needs to be changed (by appending current_path
		 * to the dest path).
		 */
		char* chdir_dest = calloc(MAX_STRING, sizeof(char));
		require(chdir_dest != NULL, "Memory initialization of chdir_dest in directory_dest failed\n");
		if(dest[0] != '/')
		{ /* The path is relative, append current_path */
			strcat(chdir_dest, current_path);
			strcat(chdir_dest, "/");
			strcat(chdir_dest, dest);
		}
		else
		{ /* The path is absolute */
			strcpy(chdir_dest, dest);
		}
		if(0 <= chdir(chdir_dest))
		{ /* chdir returned successfully */
			/*
			 * But because of M2-Planet, that doesn't mean anything actually
			 * happened, check that before we go any further.
			 */
			char* new_path = calloc(MAX_STRING, sizeof(char));
			require(new_path != NULL, "Memory initialization of new_path in directory_dest failed\n");
			getcwd(new_path, MAX_STRING);
			if(!match(current_path, new_path))
			{
				isdirectory = TRUE;
				chdir(current_path);
			}
		}
		free(chdir_dest);
		free(current_path);
	}

	/*
	 * If it isn't a directory, and we require one, error out.
	 * Otherwise, just return what we were given, we're done here.
	 */
	if(require_directory) require(isdirectory, "Provide a directory destination for multiple source files\n");
	if(!isdirectory) return dest;

	/* If it is, we need to make dest a full path */
	/* 1. Get the basename of source */
	char* basename = calloc(MAX_STRING, sizeof(char));
	require(basename != NULL, "Memory initialization of basename in directory_dest failed\n");
	int last_slash_pos = find_last_char_pos(source, '/');
	int source_len = strlen(source);
	if(last_slash_pos >= 0)
	{ /* Yes, there is a slash in it, copy over everything after that pos */
		unsigned spos; /* source pos */
		unsigned bpos = 0; /* basename pos */
		/* Do the actual copy */
		for(spos = last_slash_pos + 1; spos < source_len; spos = spos + 1)
		{
			basename[bpos] = source[spos];
			bpos = bpos + 1;
		}
	}
	else
	{ /* No, there is no slash in it, hence the basename is just the source */
		strcpy(basename, source);
	}
	/* 2. Ensure our dest (which is a directory) has a trailing slash */
	dest_len = strlen(dest);
	if(dest[dest_len - 1] != '/')
	{
		strcat(dest, "/");
	}
	/* 3. Add the basename to the end of the directory */
	strcat(dest, basename);
	free(basename);

	/* Now we have a returnable path! */
	return dest;
}

void copy_file(char* source, char* dest)
{
	int bytes;
	int written;

	if(verbose)
	{ /* Output message */
		/* Of the form 'source' -> 'dest' */
		fputs("'", stdout);
		fputs(source, stdout);
		fputs("' -> '", stdout);
		fputs(dest, stdout);
		fputs("'\n", stdout);
	}

	/* Open source and dest. */
	int fsource = open(source, 0, 0);
	if(fsource < 0)
	{
		fputs("Error opening source file ", stderr);
		fputs(source, stderr);
		fputc('\n', stderr);
		exit(EXIT_FAILURE);
	}
	int fdest = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if(fdest < 0)
	{
		fputs("Error opening destination file", stderr);
		fputs(dest, stderr);
		fputc('\n', stderr);
		exit(EXIT_FAILURE);
	}

	char* buffer = calloc(COPY_BUFFER_SIZE + 1, sizeof(char));
	require(buffer != NULL, "Memory initialization of copy buffer failed\n");
keep:
	bytes = read(fsource, buffer, COPY_BUFFER_SIZE);
	require(0 <= bytes, "Error reading source file\n");
	written = write(fdest, buffer, bytes);
	require(bytes == written, "Error writing destination file\n");
	if(COPY_BUFFER_SIZE == bytes)
	{
		goto keep;
	}

	/* Cleanup */
	require(0 == close(fsource), "Error closing source file\n");
	require(0 == close(fdest), "Error closing destination file\n");
	free(buffer);
}

int main(int argc, char** argv)
{
	/* Initialize variables */
	char** sources = calloc(MAX_ARRAY, sizeof(char*));
	require(sources != NULL, "Memory initialization of sources failed\n");
	int sources_index = 0;
	char* dest = NULL;

	/* Set defaults */
	verbose = FALSE;

	int i = 1;
	/* Loop arguments */
	while(i <= argc)
	{
		if(NULL == argv[i])
		{ /* Ignore and continue */
			i = i + 1;
		}
		else if(match(argv[i], "-h") || match(argv[i], "--help"))
		{
			fputs("Usage: ", stdout);
			fputs(argv[0], stdout);
			fputs(" [-h | --help] [-V | --version] [-v | --verbose] source1 source2 sourcen destination\n", stdout);
			exit(EXIT_SUCCESS);
		}
		else if(match(argv[i], "-V") || match(argv[i], "--version"))
		{ /* Output version */
			fputs("cp version 1.4.0\n", stdout);
			exit(EXIT_SUCCESS);
		}
		else if(match(argv[i], "-v") || match(argv[i], "--verbose"))
		{
			verbose = TRUE;
			i = i + 1;
		}
		else if(argv[i][0] != '-')
		{ /* It is not an option */
			require(sources_index < MAX_ARRAY, "Too many files\n");
			sources[sources_index] = calloc(MAX_STRING, sizeof(char));
			require(sources[sources_index] != NULL, "Memory initialization of sources[source_index] failed\n");
			strcpy(sources[sources_index], argv[i]);
			sources_index = sources_index + 1;
			i = i + 1;
		}
		else
		{ /* Unknown argument */
			fputs("UNKNOWN_ARGUMENT\n", stderr);
			exit(EXIT_FAILURE);
		}
	}

	/* Sanitize values */
	/* Ensure the two values have values */
	/* Another workaround for short-circuit bug */
	int error = FALSE;
	if(sources_index < 2) error = TRUE;
	if(error == FALSE)
	{
		dest = sources[sources_index - 1];
		sources_index = sources_index - 1;
		sources[sources_index] = NULL;
	}
	if(error == FALSE) if(sources[0] == NULL) error = TRUE;
	if(error == FALSE) if(match(sources[0], "")) error = TRUE;
	require(!error, "Provide a source file\n");
	error = FALSE;
	if(dest == NULL) error = TRUE;
	if(error == FALSE) if(match(dest, "")) error = TRUE;
	require(!error, "Provide a destination file\n");

	/* Loop through all of the sources, copying each one */
	char* this_dest;
	for(i = 0; i < sources_index; i = i + 1)
	{
		/* Convert the dest variable to a full path if it's a directory copying to */
		/*
		 * Also, if there is more than one source, we have to be copying to
		 * a directory destination...
		 */
		if(sources_index == 1)
		{
			dest = directory_dest(dest, sources[i], FALSE);
			copy_file(sources[i], dest);
		}
		else
		{
			this_dest = calloc(MAX_STRING, sizeof(char));
			require(this_dest != NULL, "Memory initalization of this_dest failed\n");
			strcpy(this_dest, dest);
			this_dest = directory_dest(this_dest, sources[i], TRUE);
			copy_file(sources[i], this_dest);
			free(this_dest);
		}
		/* Perform the actual copy */
		free(sources[i]);
	}

	free(sources);
	free(dest);

	return EXIT_SUCCESS;
}
