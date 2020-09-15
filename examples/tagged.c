/*
// This example code is distributed as a part of the
// libreadconf library, and is provided as public domain,
// free of any license restrictions, for the convenience 
// of any developers looking to use the library.
//
// As such you are free to copy, redistribute, and reuse
// this code, or any part thereof, in any way, with or 
// without credit as to it's origin.
//
// YOU MAY NOT make claim that you are the originator of
// this code (unless you really are), or attempt take 
// legal action against others for using it, or any part 
// of it!
//
// This code is distributed in the hope that it will be
// useful, but WITHOUT ANY WARRANTY, implied or otherwise.
// It is provided without guarantee of fitness for ANY
// purpose.
// In fact the creator implores you to NOT reuse it in ANY
// production program, and instead write your own, more
// appropriate, code.
//                                        --Luna
*/
#include <stdio.h>
#include <libreadconf.h>

// This example demonstrates a way of handling tagged
// configurations using the libreadconf library.
//
// Can be compiled using 'cc -lreadconf tagged.c -o
// tagged'.

int main(void)
{
	// Name should always be of length CONFIG_MAX_KEY.
	// Otherwise it may overflow.
	char name[CONFIG_MAX_KEY];
	char data[64];
	// CONFIG is used like the stdio FILE type.
	CONFIG *cfg;
	
	// Here we open the file "./tagged.conf" and store
	// the open CONFIG to the variable 'cfg', which we
	// declared above.
	//
	// config_open() returns NULL on failure, so we
	// test	for that.
	cfg = config_open("./tagged.conf");
	if(cfg == NULL)
		return 1;

	// Here we do the actual read, and test to make
	// sure it completed correctly.
	//
	// If all is well, we'll be able to look through
	// the data that we read in.
	if(config_read(cfg) == -1)
		return 1;

    // Here we demonstrate the use of the config_next(),
    // config_search(), and config_rewind() functions
    //
    // Note how the searches after searching for a tag
    // return the next occurrence of an item, starting at
    // the tag, even if there's more than one occurrence
    // of that key.
    //
    // Also note that the rewind allows us to search a 
    // "Global" item, even after reaching the end of the
    // list.
	config_next(cfg, name, data, 64);
	if(name[0] != '\0')	
		printf("Read \"%s\" with value \"%s\"\n", name, data);
	else
		return 0;

    // Note that, as the 'name' argument to config_search()
    // is a constant, we cannot use it to test for success.
    // Instead we use the return value of the function.
    //
    // GOTCHA:
    // The search functions in libreadconf use a "dumb"
    // search.
	// They only check for EXACTLY what you tell them to.
	// Example:
	// Try changing "[Section1]" to "[section1]", and watch
	// it fail.
	if(config_search(cfg, "[Section1]", data, 64))
		printf("\nJumping to Section1\n");
	else
		return 0;

	if(config_search(cfg, "Item1", data, 64))
		printf("Section1.Item1 = %s\n", data);
	else
		return 0;
	
	if(config_search(cfg, "Item2", data, 64))
		printf("Section1.Item2 = %s\n", data);
	else
		return 0;
	
	if(config_search(cfg, "[Section2]", data, 64))
		printf("\nJumping to Section2\n");
	else
		return 0;
	
	// Even though there's an "Item1" in Section1 too,
	// because we passed it, and are now in Section2 we 
	// only get the one from this tag.
	if(config_search(cfg, "Item1", data, 64))
		printf("Section2.Item1 = %s\n", data);
	else
		return 0;

	if(config_search(cfg, "Item4", data, 64))
		printf("Section1.Item4 = %s\n", data);
	else
		return 0;

    // We return to the beginning of the list with
    // config_rewind(), and look for a specific, global
    // value.
	config_rewind(cfg);
	printf("\nReturning position to start.\n");

	if(config_search(cfg, "Global2", data, 64))
		printf("\nFound Global2 with value \"%s\"\n", data);
	else
		return 0;
	
	// Close the config, because we're done with it.
	config_close(cfg);
	
	// NOTE:
	// config_next(), config_search, and config_rewind
	// can all fail, returning and error. We didn't test 
	// for it here, as it's not very likely, but in
	// production, you should.
	//
	// Read the man pages for more information.
	
	return 0;
}
