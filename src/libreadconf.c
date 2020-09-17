/*
// This file is part of libreadconf.
//
// libreadconf is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// libreadconf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with libreadconf.  If not, see <https://www.gnu.org/licenses/>.
*/
#include <sys/stat.h>
#include <string.h>
#include <signal.h>
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include "libreadconf.h"

#define SIGMASK_SET 0
#define SIGMASK_RST 1

#ifndef NO_MIN_BUFF
	#define BUFF_MIN 512
#endif

// sigprocmask() isn't thread safe, but we'll still allow
// people to use it over pthread_sigmask() if they want.
#ifdef NO_PTHREAD
    #define SIGPROCMASK_(HOW, SET, OLDSET) sigprocmask(HOW, SET, OLDSET)
#else
    #define SIGPROCMASK_(HOW, SET, OLDSET) pthread_sigmask(HOW, SET, OLDSET)
#endif

typedef struct k_list
{
	char           *name;
	char	       *value;
	struct k_list  *key_next;
} k_list;

// I'm using pointer to pointer for key_current so that we
// can free items from the current position without working 
// back down the list or doing extra assignements.
//
// You may thing the pointer assigments are a mess, but I
// can read them just fine, and I know exactly what's going
// on.
// 			-Luna
struct config
{
	int             fd;
	size_t          block_size;
	size_t	        buff_size;
	char           *buff;
	size_t          buff_pos;
	k_list         *key_list;
	k_list        **key_current;
};

/*
 * I started by declaring my static function here at the top.
 * I may move them to (a) header(s) later.	
 * 			-Luna
 */

// We have a function to get a file's blocksize using fstat().
// We use it to determine what buffer size to start with.
static ssize_t get_block_size(int fd)
{
	struct stat tmp;
	
	if(fstat(fd, &tmp) < 0)
		return -1;
	else
		return tmp.st_blksize;
}

// Because space isn't the only whitespace character, we have
// a function to test if a character is whitespace rather than
// making our if() statements even harder to read.
static int is_whitespace(char ctest)
{
	if(ctest == ' ' || ctest == '\t' || ctest == '\r')
		return 1;
	else
		return 0;
}

// I decided on this over using strcmp() becaue we only care
// if the strings are identical or not.
//
// We don't need to waste time on comparing strings that don't
// match.
static int fast_cmp(const char *restrict str1, const char *restrict str2)
{
	for(int i = 0; str1[i] != '\0'; i++)
	{
		if(str1[i] != str2[i])
			return 0;
	}

	return 1;
}

// Here's our 'magic' signal blocking function.
// Tt handles both setting and resetting the signal mask when-
// ever we enter any of the public functions.
static int set_sigmask(int state)
{
	static sigset_t old_mask;
	
	if(state == SIGMASK_SET)
	{
		sigset_t new_mask;
		if(sigemptyset(&new_mask) != 0)
			return 0;
		if(sigaddset(&new_mask, SIGHUP) != 0)
			return 0;

		if(SIGPROCMASK_(SIG_BLOCK, &new_mask, &old_mask) != 0)
			return 0;
	}
	else if(state == SIGMASK_RST)
	{
		if(SIGPROCMASK_(SIG_SETMASK, &old_mask, NULL) != 0)
		{
			// If we fail to reset the mask we're kind of SOL.
			// We'll just crash and *try* to print an error to
			// stderr
			write(STDERR_FILENO, "libreadconf: Failed to reset sigmask!\n", 39);
			abort();
			 // It shouldn't be possible to ever get here, but for safety.
			_exit(1);
		}
	}

	return 1;
}

/*
 * Here we start our static functions for allocating
 * handing, and freeing our linked-list.
 */

// Adds a new entry *at the end* of a linked-list.
static int list_add(k_list **restrict list)
{
	if(*list == NULL)
	{
		*list = malloc(sizeof(k_list));
		if(*list == NULL)
			return -1;
		
		(*list)->name = NULL;
		(*list)->key_next = NULL;
		
		return 0;
	}

	k_list *current = *list;
	
	int count = 0;

	while(current->key_next != NULL)
	{
		count++;
		current = current->key_next;
	}

	current->key_next = malloc(sizeof(k_list));
	if(current->key_next == NULL)
		return -1;
	
	current = current->key_next;
	current->name = NULL;
	current->key_next = NULL;

	return count;
}

// This is the pimitive used to implement the index
// functions.
// It returns the n-1th element from the element passed
// to it.
static k_list *list_get(int index, k_list *restrict list)
{
	if(list == NULL)
	{
		errno = EINVAL;
		return NULL;
	}
	
	k_list *current = list;

	for(int i=0;i<index;i++)
	{
		if(current->key_next == NULL)
		{
			errno = 0;
			return NULL;
		}

		current = current->key_next;
	}

	return current;
}

// Note that our function for freeing a list takes a 
// pointer to pointer.
// This lets us start in the middle of a list and set our
// starting point to NULL in one step.
static void list_free(k_list **restrict list)
{
	if(*list == NULL)
		return;

	k_list *current = *list;
	k_list *next = current->key_next;

	while(current != NULL)
	{
		if(current->name != NULL)
			free(current->name);
		free(current);
		current = NULL;
		
		current = next;
		if(current != NULL)
			next = current->key_next;
	}

	*list = NULL;	

	return;
}

/*
 * Here we start our static functions for parsing our keys.
 */

// This function gets the next item in 'buff' starting at 
// '*position' and puts it into the 'name' value of a list item.
// At this point, the item is a single string.
static int get_next_key(k_list *restrict key_out, char *restrict buff, size_t *position)
{
	if(key_out == NULL || buff == NULL || position == NULL)
	{
		errno = EINVAL;
		return -1;
	}

	ssize_t start = -1;
	ssize_t end = -1;
    
	while((start == -1 || buff[*position] != '\n') && buff[*position] != '\0')
	{
		if(buff[*position] == '#')
			for(;buff[*position] != '\n' && buff[*position] != '\0';(*position)++);
		else if((start == -1 && !is_whitespace(buff[*position]) && buff[*position] != '\n'))
		{
			start = *position;
			end = ++(*position);
		}
		else if(start == -1 && key_out->name != NULL)
		{
		    start = 0;
		    end = ++(*position);
		}
		else if(!is_whitespace(buff[*position]))
			end = ++(*position);
		else
			(*position)++;
	}
	
	if(buff[*position] == '\0')
	    end = (*position);
	
	if(key_out->name == NULL && start != -1)
	{
		key_out->name = malloc(end - start + 1);
		if(key_out->name == NULL)
			return -1;

		memcpy(key_out->name, (buff + start), (end - start));
		key_out->name[end - start] = '\0';
		
		if(buff[*position] == '\n')
			return 0;
		else
			return (end - start);
	}
	else if(start != -1)
	{	
		size_t old_len = strlen(key_out->name);
		char *tmp = NULL;

		tmp = realloc(key_out->name, (old_len + (end - start + 1)));
		if(tmp == NULL)
			return -1;

		key_out->name = tmp;

		memcpy((key_out->name + old_len), (buff + start), (end - start));
		key_out->name[old_len + (end - start)] = '\0';

		if(buff[*position] == '\n')
			return 0;
		else
			return (end - start);
	}
	else
		return -2;
}

// This function take the list item with a single string returned by 
// get_next_key(), and parses it into two strings.
//
// Note that while we get two strings, we're still only using a 
// single buffer.
// This saves us doing extra allocations or copies.
static int key_parse(k_list *restrict key)
{
	if(key == NULL || key->name == NULL)
	{
		errno = EINVAL;
		return -1;
	}

	char is_key = 1;
	size_t position;
	       
	ssize_t	start = -1, end;

	for(position = 0; key->name[position] != '=' && key->name[position] != '\0'; position++)
	{
		if(!is_whitespace(key->name[position]) && start == -1)
		{
			start = position;
			end = position + 1;
		}
		else if(!is_whitespace(key->name[position]))
			end = position + 1;
	}
	
	if(key->name[position] == '=')
	{
		is_key = 1;
		key->name[position] = ' ';
	}

	if(start > 0)
	{
		memmove(key->name, (key->name + start), (end - start));
		key->name[end - start] = '\0';
	}
	else
		key->name[end - start] = '\0';

	key->value = (key->name + (end - start + 1));

	start = -1;

	for(position = 0; key->value[position] != '\0'; position++)
	{
		if(!is_whitespace(key->value[position]) && start == -1)
		{
			start = position;
			end = position + 1;
		}
		else if(!is_whitespace(key->value[position]))
			end = position + 1;
	}

	if(start > 0)
	{
		memmove(key->value, (key->value + start), (end - start));
		key->value[end - start] = '\0';
	}
	else
		key->value[end - start] = '\0';

	char *tmp;
	
	// Note how we reallocate the 'name' element of the list item
	// This should save space, but may just result in extra work
	// in some cases.
	//
	// Should we include a directive to skip this step?
	if(!strlen(key->value) && is_key)
	{
		tmp = realloc(key->name, (strlen(key->name) + 3));
		if (tmp != NULL)
			key->name = tmp;
		else
			return -1;

		key->value[0] = '\n';
		key->value[1] = '\0';
	}
	if(!strlen(key->value) && !is_key)
	{
		tmp = realloc(key->name, (strlen(key->name) + 2));
		if (tmp != NULL)
			key->name = tmp;
		else
			return -1;

		key->value[0] = '\0';
	}
	else
	{
		tmp = realloc(key->name, (strlen(key->name) + strlen(key->value) + 2));
		if (tmp != NULL)
			key->name = tmp;
		else
			return -1;
	}

	return 0;
}

/*
 * Here we get into the public functions of the library.
 * This should be the only part most people interact with.
 *
 * If you want descriptions of them, check the manpages.
 */
 
CONFIG *config_open(const char *restrict path)
{
	if(!set_sigmask(SIGMASK_SET))
		return NULL;

	CONFIG *init = malloc(sizeof(CONFIG));
	if(init == NULL)
	{
		set_sigmask(SIGMASK_RST);
		return NULL;
	}

	init->fd = open(path, O_RDONLY);
	if(init->fd < 0)
		goto fail;
	// I'm using goto for error handling.
	// Eat me.
	// 			-Luna
	
	init->block_size = get_block_size(init->fd);
	if(init->block_size < 0)
		goto fail;

	init->buff = NULL;
	init->key_list = NULL;
	init->key_current = NULL;
	
	set_sigmask(SIGMASK_RST);
	return init;

	fail:
		set_sigmask(SIGMASK_RST);
		free(init);
		return NULL;
}

// This one's not quite done, but it's here.
// We're missing some checking to ensure the we can actually
// use the descriptor we were passed.
CONFIG *config_fdopen(int fd)
{
	if(!set_sigmask(SIGMASK_SET))
		return NULL;

	CONFIG *init = malloc(sizeof(CONFIG));
	if(init == NULL)
	{
		set_sigmask(SIGMASK_RST);
		return NULL;
	}

	init->fd = fd;

	init->block_size = get_block_size(init->fd);
	if(init->block_size < 0)
		goto fail;
	
	init->buff = NULL;
	init->key_list = NULL;
	init->key_current = NULL;

	set_sigmask(SIGMASK_RST);
	return init;

	fail:
		set_sigmask(SIGMASK_RST);
		free(init);
		return NULL;
}

CONFIG *config_reopen(const char *restrict path, CONFIG *cfg)
{
	if(!set_sigmask(SIGMASK_SET))
		return NULL;

	if(close(cfg->fd) != 0)
	{
		set_sigmask(SIGMASK_RST);
		return NULL;
	}
	list_free(&cfg->key_list);
	free(cfg->buff);
	free(cfg);

	CONFIG *init = malloc(sizeof(CONFIG));
	if(init == NULL)
	{
		set_sigmask(SIGMASK_RST);
		return NULL;
	}

	init->fd = open(path, O_RDONLY);
	if(init->fd < 0)
		goto fail;
	
	init->block_size = get_block_size(init->fd);
	if(init->block_size < 0)
		goto fail;

	init->buff = NULL;
	init->key_list = NULL;
	init->key_current = NULL;
	
	set_sigmask(SIGMASK_RST);
	return init;

	fail:
		set_sigmask(SIGMASK_RST);
		free(init);
		return NULL;
}

int config_read(CONFIG *restrict cfg)
{
	if(!set_sigmask(SIGMASK_SET))
		return -1;

	if(cfg == NULL)
	{
		errno = EINVAL;
		set_sigmask(SIGMASK_RST);
		return -1;
	}
	
	// We'll allow people to choose whether they want to 
	// use a minimum buffer size.
	//
	// Depending on the envinronment, this may or may not
	// be useful.
	#ifdef NO_MIN_BUFF
	cfg->buff_size = cfg_block_size;
	#else
	if(cfg->block_size < BUFF_MIN)
		cfg->buff_size = cfg->block_size;
	else
		cfg->buff_size = BUFF_MIN;
	#endif		

	// I was using this line to debug issues with parsing
	// between buffers.
	// I'll leave it here for now.
	// 			-Luna
	//cfg->buff_size = 32;

	cfg->buff = malloc(cfg->buff_size + 1);
	if(cfg->buff == NULL)
	{
		set_sigmask(SIGMASK_RST);
		return -1;
	}
	cfg->buff_pos = 0;
	cfg->buff[cfg->buff_size] = '\0';

	int reading = 1;
	ssize_t state;

	while(reading)
	{
		errno = 0;

		state = read(cfg->fd, cfg->buff, cfg->buff_size);
		if(state == -1)
			goto fail;
		else if(state == 0)
		{
			if(errno != EAGAIN || errno != EINTR)
				reading = 0;
		}
		else
			cfg->buff_pos=0;
		
		for(int i=0;i < state;i++)
		{
			if(cfg->buff[i] == '\0')
				cfg->buff[i] = ' ';
		}

		cfg->buff[state] = '\0';

		
		if(cfg->key_list == NULL)
		{
			state = list_add(&cfg->key_list);
			if(cfg->key_list == NULL)
				goto fail;	
			cfg->key_current = &cfg->key_list;
		}
		
		state = 0;

		while(reading)
		{
			state = get_next_key(*cfg->key_current, cfg->buff, &cfg->buff_pos);
			if(state == -1)
				goto fail;

			// We break here if it's time to exit 
			// to avoid adding an extra list entry
			// (though that can still happen...)
			// or closing everything in an if().
			//
			// Just feels a bit cleaner, for now.
			// 			-Luna
			if(state > 0 || state == -2)
				break;
			
			if(list_add(cfg->key_current) == -1)
				goto fail;

			cfg->key_current = &(*cfg->key_current)->key_next;
		}
	}
	
	// Due to a quirk of get_next_key(), we may have 
	// accidentally allocated an empty key at the end of our
	// list.
	//
	// I'f that's the case, we need to free it, or it may
	// cause problems.
	//
	// There's probably a better solution to this, but this
	// is fine for now.	
	// 			-Luna
	if((*cfg->key_current)->name == NULL)
		list_free(cfg->key_current);

	cfg->key_current = &cfg->key_list;

	while(*cfg->key_current != NULL)
	{
		state = key_parse(*cfg->key_current);
		if(state == -1)
			goto fail;
		
		cfg->key_current = &(*cfg->key_current)->key_next;
	}

	cfg->key_current = &cfg->key_list;

	set_sigmask(SIGMASK_RST);
	return 0;

	fail:
		set_sigmask(SIGMASK_RST);
		list_free(&cfg->key_list);
		free(cfg->buff);
		return -1;

}

void config_rewind(CONFIG *restrict cfg)
{
	if(!set_sigmask(SIGMASK_SET))
		return;

	if(cfg == NULL || cfg->key_list == NULL)
	{  
		errno = EINVAL;

		set_sigmask(SIGMASK_RST);
		return;
	}

	cfg->key_current = &cfg->key_list;

	set_sigmask(SIGMASK_RST);
	return;
}

int config_close(CONFIG *restrict cfg)
{
	if(!set_sigmask(SIGMASK_SET))
		return -1;

	if(cfg == NULL)
	{
		errno = EINVAL;

		set_sigmask(SIGMASK_RST);
		return -1;
	}
	
	// Should this be "free and close", or "close and free"?
	// Either way, if close() fails, we're left in an 
	// undefined state.
	// 			-Luna
	if(close(cfg->fd) != 0)
	{
		set_sigmask(SIGMASK_RST);
		return -1;
	}
	list_free(&cfg->key_list);
	free(cfg->buff);
	free(cfg);

	set_sigmask(SIGMASK_RST);
	return 0;
}

// If you're asking why these functions return -1 and 0, I don't
// know. It was an arbitrary choise that I should probably change.
// 			-Luna
int config_index(CONFIG *restrict cfg, char *restrict name, char *restrict data_buff, unsigned int buff_size, unsigned int index)
{
	if(!set_sigmask(SIGMASK_SET))
		return -1;

	if(name == NULL || data_buff == NULL)
	{
		errno = EINVAL;
		set_sigmask(SIGMASK_RST);
		return -1;
	}

	if(cfg == NULL || cfg->key_list == NULL)
	{
		name[0] = '\0';
		data_buff[0] = '\0';

		errno = EINVAL;
		set_sigmask(SIGMASK_RST);
		return -1;
	}

	k_list *tmp;

	tmp = list_get(index, cfg->key_list);
	if(tmp == NULL)
	{
		name[0] = '\0';
		data_buff[0] = '\0';

		set_sigmask(SIGMASK_RST);
		return 0;
	}
	else
	{
		if(strlen(tmp->name) > (CONFIG_MAX_KEY - 1))
		{
			memcpy(name, tmp->name, (CONFIG_MAX_KEY));
			name[CONFIG_MAX_KEY - 1] = '\0';
		}
		else
			memcpy(name, tmp->name, (strlen(tmp->name) + 1));

		if(strlen(tmp->value) > (buff_size - 1))
		{
			memcpy(data_buff, tmp->value, (buff_size));
			data_buff[buff_size - 1] = '\0';

			set_sigmask(SIGMASK_RST);
			return(strlen(tmp->value) - buff_size);
		}
		else
			memcpy(data_buff, tmp->value, (strlen(tmp->value) + 1));
	}

	set_sigmask(SIGMASK_RST);
	return 1;
}

int config_search(CONFIG *restrict cfg, const char *restrict name, char *restrict data_buff, unsigned int buff_size)
{
	if(!set_sigmask(SIGMASK_SET))
		return -1;

	if(cfg == NULL || cfg->key_current == NULL || name == NULL)
	{
		if(data_buff != NULL)
			data_buff[0] = '\0';

		errno = EINVAL;
		set_sigmask(SIGMASK_RST);
		return -1;
	}

	while(1)
	{
		if(*cfg->key_current == NULL || fast_cmp((*cfg->key_current)->name, name))
			break;

		cfg->key_current = &(*cfg->key_current)->key_next;
	}

	if(*(cfg->key_current) == NULL)
	{
		if(data_buff != NULL)
			data_buff[0] = '\0';

		set_sigmask(SIGMASK_RST);
		return 0;
	}
	else
	{
		if(data_buff != NULL && strlen((*cfg->key_current)->value) > (buff_size - 1))
		{
			memcpy(data_buff, (*cfg->key_current)->value, (buff_size));
			data_buff[buff_size - 1] = '\0';
			cfg->key_current = &(*cfg->key_current)->key_next;

			set_sigmask(SIGMASK_RST);
			return(strlen((*cfg->key_current)->value) - buff_size);
		}
		else if(data_buff != NULL)
			memcpy(data_buff, (*cfg->key_current)->value, (strlen((*cfg->key_current)->value) + 1));

		cfg->key_current = &(*cfg->key_current)->key_next;

		set_sigmask(SIGMASK_RST);
		return 1;
	}
}

int config_next(CONFIG *restrict cfg, char *restrict name, char *restrict data_buff, unsigned int buff_size)
{
	if(!set_sigmask(SIGMASK_SET))
		return -1;

	if(name == NULL || data_buff == NULL)
	{
		errno = EINVAL;
		set_sigmask(SIGMASK_RST);
		return -1;
	}

	if(cfg == NULL || cfg->key_current == NULL)
	{
		name[0] = '\0';
		data_buff[0] = '\0';

		errno = EINVAL;
		set_sigmask(SIGMASK_RST);
		return -1;
	}

	if(*cfg->key_current == NULL)
	{
		name[0] = '\0';
		data_buff[0] = '\0';

		set_sigmask(SIGMASK_RST);
		return 0;
	}
	else
	{
		if(strlen((*cfg->key_current)->name) > (CONFIG_MAX_KEY - 1))
		{
			memcpy(name, (*cfg->key_current)->name, (CONFIG_MAX_KEY));
			name[CONFIG_MAX_KEY - 1] = '\0';
		}
		else
			memcpy(name, (*cfg->key_current)->name, (strlen((*cfg->key_current)->name) + 1));

		if(strlen((*cfg->key_current)->value) > (buff_size - 1))
		{
			memcpy(data_buff, (*cfg->key_current)->value, (buff_size));
			data_buff[buff_size - 1] = '\0';
			cfg->key_current = &(*cfg->key_current)->key_next;

			set_sigmask(SIGMASK_RST);
			return(strlen((*cfg->key_current)->value) - buff_size);
		}
		else
			memcpy(data_buff, (*cfg->key_current)->value, (strlen((*cfg->key_current)->value) + 1));
	}

	cfg->key_current = &(*cfg->key_current)->key_next;

	set_sigmask(SIGMASK_RST);
	return 1;
}

// The by-reference function were originaly a debug tool, but
// ended up being useful, so I left them in as a feature.
// 			-Luna
void config_index_br(CONFIG *restrict cfg, char **restrict name, char **restrict data, unsigned int index)
{
	if(!set_sigmask(SIGMASK_SET))
		return;

	if(name == NULL || data == NULL)
	{
		errno = EINVAL;
		set_sigmask(SIGMASK_RST);
		return;
	}

	if(cfg == NULL || cfg->key_list == NULL)
	{
		*name = NULL;
		*data = NULL;

		errno = EINVAL;
		set_sigmask(SIGMASK_RST);
		return;
	}

	k_list *tmp;

	tmp = list_get(index, cfg->key_list);
	if(tmp == NULL)
	{
		*name = NULL;
		*data = NULL;

		set_sigmask(SIGMASK_RST);
		return;
	}
	else
	{
		*name = tmp->name;
		*data = tmp->value;

		set_sigmask(SIGMASK_RST);
		return;
	}
}

void config_search_br(CONFIG *restrict cfg, const char *restrict name, char **restrict data)
{
	if(!set_sigmask(SIGMASK_SET))
		return;

	if(cfg == NULL || cfg->key_current == NULL || *cfg->key_current == NULL || name == NULL)
	{
		if(data != NULL)
			*data = NULL;

		errno = EINVAL;
		set_sigmask(SIGMASK_RST);
		return;
	}
	
	while(1)
	{
		if(*cfg->key_current == NULL || fast_cmp((*cfg->key_current)->name, name))
			break;

		cfg->key_current = &(*cfg->key_current)->key_next;
	}

	if(*cfg->key_current == NULL)
	{
		if(data != NULL)
			*data = NULL;

		set_sigmask(SIGMASK_RST);
		return;
	}
	else
	{
		if(data != NULL)
			*data = (*cfg->key_current)->value;
		cfg->key_current = &(*cfg->key_current)->key_next;

		set_sigmask(SIGMASK_RST);
		return;
	}
}

void config_next_br(CONFIG *restrict cfg, char **restrict name, char **restrict data)
{
	if(!set_sigmask(SIGMASK_SET))
		return;

	if(name == NULL || data == NULL)
	{
		errno = EINVAL;
		set_sigmask(SIGMASK_RST);
		return;
	}

	if(cfg == NULL || cfg->key_list == NULL)
	{
		*name = NULL;
		*data = NULL;

		errno = EINVAL;
		set_sigmask(SIGMASK_RST);
		return;
	}

	if(*cfg->key_current == NULL)
	{
		*name = NULL;
		*data = NULL;

		set_sigmask(SIGMASK_RST);
		return;
	}
	else
	{
		*name = (*cfg->key_current)->name;
		*data = (*cfg->key_current)->value;

		set_sigmask(SIGMASK_RST);
		return;
	}
}
