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

// This example demonstrates the use of the by-reference
// functions provided by the libreadconf library.
// It is reccomended that you look at the other examples
// first.
//
// Can be compiled using 'cc -lreadconf by-ref.c -o
// by-ref'.

int main (void)
{
	// Note that we simply use 'char *' rather than
	// declaring the lenght, as in the other examples.
	char *name;
	char *data;
	CONFIG *cfg;

	// We'll start with demonstrating the by-reference
	// versions of the functions used in the "tagged.c"
	// example.
	// So we first open and read the "tagged.conf" file
	// and checking for errors.
	cfg = config_open("./tagged.conf");
	if(cfg == NULL)
		return 1;

	if(config_read(cfg) == -1)
		return 1;

	printf("Using congiguration: tagged.conf\n\n");

	// Note that the by-reference functions will return
	// NULL annd not '\0' if they reach the end of the 
	// keylist.
	//
	// Also note that the by-reference functions take a
	// 'char **', so we have to pass the addresses of
	// 'name' and 'data', not just their value. 
	config_next_br(cfg, &name, &data);
	if(name != NULL)
		printf("Read \"%s\" with value \"%s\"\n", name, data);
	else
		return 0;

	//+To-do:
	//+Add return values to the by-reference functions.
	//+We have no way of checking if this suceeded or not.
	config_search_br(cfg, "[Section1]", NULL);
	printf("\nJumping to Section1\n");

	config_next_br(cfg, &name, &data);
	if(name != NULL)
		printf("Section1.%s = %s\n", name, data);
	else
		return 0;

	config_next_br(cfg, &name, &data);
	if(name != NULL)
		printf("Section1.%s = %s\n", name, data);
	else
		return 0;

	// Here we demonstrate the index loop used in the
	// "basic.c" example.
	//
	// We start be reopening 'cfg' as "basic.conf",
	// rereading it, and testing for errors.	
	cfg = config_reopen("./basic.conf", cfg);
	if(cfg == NULL)
		return 1;

	if(config_read(cfg) == -1)
		return 1;

	printf("\nUsing configuration: basic.conf\n\n");

	// Here we have the same loop from "basic.c" except
	// using config_index_br().
	//
	// The only real change is testing if name is NULL,
	// rether than '\0'.
	for(int i = 0; i < 1000; i++)
	{
		config_index_br(cfg, &name, &data, i);

		if(name == NULL)
			break;
		
		if(data[0] != '\n')
			printf("%s = %s\n", name, data);
		else
			printf("%s\n", name);
	}

	// As always, we close the config when we're done.
	config_close(cfg);

	// NOTE:
	// As usual, we preformened minimal error checking.
	// Always remember to read the manual, and know what
	// errors you should be testing for.

	return 0;
}
