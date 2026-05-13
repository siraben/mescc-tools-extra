/* Copyright (C) 2019 Jeremiah Orians
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
#include "M2libc/bootstrappable.h"

#define OUTPUT_BUFFER_SIZE 4096

char* input_name;
FILE* input;
char* output_name;
FILE* output;
char* pattern;
size_t pattern_length;
char* replacement;
char* buffer;
size_t buffer_index;
char* hold;
char* output_buffer;
int output_buffer_index;

void flush_output()
{
	if(0 == output_buffer_index) return;
#ifdef __M2__
	require(output_buffer_index == write(output->fd, output_buffer, output_buffer_index), "incomplete write of output\n");
#else
	require(output_buffer_index == fwrite(output_buffer, 1, output_buffer_index, output), "incomplete write of output\n");
#endif
	output_buffer_index = 0;
}

void write_output_byte(int c)
{
	if(0 == c) return;

	output_buffer[output_buffer_index] = c;
	output_buffer_index = output_buffer_index + 1;
	if(OUTPUT_BUFFER_SIZE == output_buffer_index) flush_output();
}

void write_output_string(char* s)
{
	size_t i = 0;
	while(0 != s[i])
	{
		write_output_byte(s[i]);
		i = i + 1;
	}
}

void read_next_byte()
{
	int c= hold[0];
	size_t i = 0;
	while(i < pattern_length)
	{
		hold[i] = hold[i+1];
		i = i + 1;
	}

	hold[pattern_length-1] = buffer[buffer_index];
	buffer_index = buffer_index + 1;

	/* NEVER WRITE NULLS!!! */
	write_output_byte(c);
}

void clear_hold()
{
	/* FILL hold with NULLS */
	size_t i = 0;
	while(i < pattern_length)
	{
		hold[i] = 0;
		i = i + 1;
	}
}

void check_match()
{
	/* Do the actual replacing */
	if(match(pattern, hold))
	{
		write_output_string(replacement);
		clear_hold();
	}
}

int main(int argc, char** argv)
{
	output_name = "/dev/stdout";
	pattern = NULL;
	replacement = NULL;
	buffer_index = 0;

	int i = 1;
	while (i < argc)
	{
		if(NULL == argv[i])
		{
			i = i + 1;
		}
		else if(match(argv[i], "-f") || match(argv[i], "--file"))
		{
			input_name = argv[i+1];
			require(NULL != input_name, "the --file option requires a filename to be given\n");
			i = i + 2;
		}
		else if(match(argv[i], "-o") || match(argv[i], "--output"))
		{
			output_name = argv[i+1];
			require(NULL != output_name, "the --output option requires a filename to be given\n");
			i = i + 2;
		}
		else if(match(argv[i], "-m") || match(argv[i], "--match-on"))
		{
			pattern = argv[i+1];
			require(NULL != pattern, "the --match-on option requires a string to be given\n");
			i = i + 2;
		}
		else if(match(argv[i], "-r") || match(argv[i], "--replace-with"))
		{
			replacement = argv[i+1];
			require(NULL != replacement, "the --replace-with option requires a string to be given\n");
			i = i + 2;
		}
		else if(match(argv[i], "-h") || match(argv[i], "--help"))
		{
			fputs("Usage: ", stderr);
			fputs(argv[0], stderr);
			fputs(" --file $input", stderr);
			fputs(" --match-on $string", stderr);
			fputs(" --replace-with $string", stderr);
			fputs(" [--output $output] (or it'll dump to stdout)\n", stderr);
			fputs("--help to get this message\n", stderr);
			exit(EXIT_SUCCESS);
		}
		else
		{
			fputs("Unknown option:", stderr);
			fputs(argv[i], stderr);
			fputs("\nAborting to avoid problems\n", stderr);
			exit(EXIT_FAILURE);
		}
	}

	/* Sanity check that we got everything we need */
	require(NULL != input_name, "You need to pass an input file with --file\n");
	require(NULL != output_name, "You need to pass an output file with --output\n");
	require(NULL != pattern, "You can't do a replacement without something to match on\n");
	require(NULL != replacement, "You can't do a replacement without something to replace it with\n");

	input = fopen(input_name, "r");
	require(NULL != input, "unable to open requested input file!\n");

#ifdef __M2__
	size_t size = input->buflen;
#else
	/* Get enough buffer to read it all */
	fseek(input, 0, SEEK_END);
	size_t size = ftell(input);
#endif

	/* Save ourself work if the input file is too small */
	pattern_length = strlen(pattern);
	require(0 < pattern_length, "replacement pattern must not be empty\n");
	require(pattern_length <= size, "input file is to small for pattern\n");
#ifdef __M2__
	buffer = input->buffer;
#else
	buffer = calloc(size + pattern_length + 8, sizeof(char));
	require(NULL != buffer, "input buffer allocation failed\n");

	/* Now read it all into buffer */
	fseek(input, 0, SEEK_SET);
	size_t r = fread(buffer,sizeof(char), size, input);
	require(r == size, "incomplete read of input\n");
	fclose(input);
#endif

	/* Now we can safely open the output (which could have been the same as the input */
	output = fopen(output_name, "w");
	require(NULL != output, "unable to open requested output file!\n");
	output_buffer = calloc(OUTPUT_BUFFER_SIZE, sizeof(char));
	require(NULL != output_buffer, "output buffer allocation failed\n");
	output_buffer_index = 0;

	/* build our match buffer */
	hold = calloc(pattern_length + 4, sizeof(char));
	require(NULL != hold, "temp memory allocation failed\n");

	/* Replace it all */
	while((size + pattern_length + 4) >= buffer_index)
	{
		read_next_byte();
		check_match();
	}
	flush_output();
	fclose(output);
#ifdef __M2__
	fclose(input);
#endif
}
