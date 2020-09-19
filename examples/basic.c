/*
 * This example code is distributed as a part of the
 * libreadconf library, and is provided as public domain,
 * free of any license restrictions, for the convenience 
 * of any developers looking to use the library.
 *
 * As such you are free to copy, redistribute, and reuse
 * this code, or any part thereof, in any way, with or 
 * without credit as to it's origin.
 *
 * YOU MAY NOT make claim that you are the originator of
 * this code (unless you really are), or attempt take 
 * legal action against others for using it, or any part 
 * of it!
 *
 * This code is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY, implied or otherwise.
 * It is provided without guarantee of fitness for ANY
 * purpose.
 * In fact the creator implores you to NOT reuse it in ANY
 * production program, and instead write your own, more
 * appropriate, code.
 *                                        --Luna
 */
#include <stdio.h>
#include <libreadconf.h>

// This example demonstrates the most basic way of using
// libreadconf to load a config file.
//
// Can be compiled with 'cc -lreadconf basic.c -o basic'.

int main(void)
{
	// Name should always be of length CONFIG_MAX_KEY.
	// Otherwise it may overflow.
	char name[CONFIG_MAX_KEY] = " ";
	char data[64];
	// CONFIG is used like the stdio FILE type.
	CONFIG *cfg;
	
	// Here we open the file "./basic.conf" and store
	// the open CONFIG to the variable 'cfg', which we
	// declared above.
	//
	// config_open() returns NULL on failure, so we
	// test	for that.
	cfg = config_open("./basic.conf");
	if(cfg == NULL)
		return 1;

	// Here we do the actual read, and test to make
	// sure it completed correctly.
	//
	// If all is well, we'll be able to look through
	// the data that we read in.
	if(config_read(cfg) == -1)
		return 1;

	// Here we use config_index() to loop through all
	// the items that we read.
	// If we encounter an item that doesn't have the 
	// '=' symbol, 'data' will contain "\n", so we test
	// for that.
	//
	// Once config_index() returns an empty name, we
	// know we've reached the end of the list. 
	// When  that happens, we break from the loop and
	// exit normally.
	for(int i = 0; i < 1000; i++)
	{
		config_index(cfg, name, data, 64, i);

		if(name[0] == '\0')
			break;
		
		if(data[0] != '\n')
			printf("The value of \"%s\" is \"%s\".\n", name, data);
		else
			printf("Item \"%s\" is not a key.\n", name);
	}

	// Close the config, because we're done with it.
	config_close(cfg);
	
	// NOTE:
	// config_index() can fail, returning and error.
	// We didn't test for it here, as it's not very 
	// likely, but in production, you should.
	//
	// Read the man pages for more information.

	return 0;
}
