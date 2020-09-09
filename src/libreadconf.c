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

static ssize_t get_block_size(int fd)
{
	struct stat tmp;
	
	if(fstat(fd, &tmp) < 0)
		return -1;
	else
		return tmp.st_blksize;
}

static int is_whitespace(char ctest)
{
	if(ctest == ' ' || ctest == '\t' || ctest == '\r')
		return 1;
	else
		return 0;
}

static int fast_cmp(const char *restrict str1, const char *restrict str2)
{
	for(int i = 0; str1[i] != '\0'; i++)
	{
		if(str1[i] != str2[i])
			return 0;
	}

	return 1;
}

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
		SIGPROCMASK_(SIG_SETMASK, &old_mask, NULL);
		// If we fail to reset the mask we're kind of SOL.
		// Should we just crash the program?

	return 1;
}

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

		memcpy(key_out->name, &buff[start], (end - start));
		*(key_out->name + (end - start)) = '\0';
		
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

		memcpy((key_out->name + old_len), &buff[start], (end - start));
		*(key_out->name + (old_len + (end - start))) = '\0';

		if(buff[*position] == '\n')
			return 0;
		else
			return (end - start);
	}
	else
		return -2;
}

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

	for(position = 0; *(key->name + position) != '=' && *(key->name + position) != '\0'; position++)
	{
		if(!is_whitespace(*(key->name + position)) && start == -1)
		{
			start = position;
			end = position + 1;
		}
		else if(!is_whitespace(*(key->name + position)))
			end = position + 1;
	}
	
	if(*(key->name + position) == '=')
	{
		is_key = 1;
		*(key->name + position) = ' ';
	}

	if(start > 0)
	{
		memmove(key->name, (key->name + start), (end - start));
		*(key->name + (end - start)) = '\0';
	}
	else
		*(key->name + (end - start)) = '\0';

	key->value = (key->name + (end - start + 1));

	start = -1;

	for(position = 0; *(key->value + position) != '\0'; position++)
	{
		if(!is_whitespace(*(key->value + position)) && start == -1)
		{
			start = position;
			end = position + 1;
		}
		else if(!is_whitespace(*(key->value + position)))
			end = position + 1;
	}

	if(start > 0)
	{
		memmove(key->value, (key->value + start), (end - start));
		*(key->value + (end - start)) = '\0';
	}
	else
		*(key->value + (end - start)) = '\0';

	char *tmp;

	if(!strlen(key->value) && is_key)
	{
		tmp = realloc(key->name, (strlen(key->name) + 3));
		if (tmp != NULL)
			key->name = tmp;
		else
			return -1;

		*(key->value) = '\n';
		*(key->value + 1) = '\0';
	}
	if(!strlen(key->value) && !is_key)
	{
		tmp = realloc(key->name, (strlen(key->name) + 2));
		if (tmp != NULL)
			key->name = tmp;
		else
			return -1;

		*(key->value) = '\0';
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
	list_free(&(cfg->key_list));
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
	
	if(cfg->block_size < 512)
		cfg->buff_size = cfg->block_size;
	else
		cfg->buff_size = 512;
		
	// debuging
	//cfg->buff_size = 32;

	cfg->buff = malloc(cfg->buff_size + 1);
	if(cfg->buff == NULL)
	{
		set_sigmask(SIGMASK_RST);
		return -1;
	}
	cfg->buff_pos = 0;
	*(cfg->buff + cfg->buff_size) = '\0';

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
			if(*(cfg->buff + i) == '\0')
				*(cfg->buff +i) = ' ';
		}

		*(cfg->buff + state) = '\0';

		
		if(cfg->key_list == NULL)
		{
			state = list_add(&(cfg->key_list));
			if(cfg->key_list == NULL)
				goto fail;	
			cfg->key_current = &(cfg->key_list);
		}
		
		state = 0;

		while(reading)
		{
			state = get_next_key(*(cfg->key_current), cfg->buff, &(cfg->buff_pos));
			if(state == -1)
				goto fail;

			// We break here if it's time to exit to avoid adding an extra list entry
			// or closing everything in an if().
			//
			// Just feels a bit cleaner, for now.
			// 			-Luna
			if(state > 0 || state == -2)
				break;
			
			if(list_add(cfg->key_current) == -1)
				goto fail;

			cfg->key_current = &((*cfg->key_current)->key_next);
		}
	}
	
	if((*cfg->key_current)->name == NULL)
		list_free(cfg->key_current);

	cfg->key_current = &(cfg->key_list);

	while(*(cfg->key_current) != NULL)
	{
		state = key_parse(*(cfg->key_current));
		if(state == -1)
			goto fail;
		
		cfg->key_current = &((*cfg->key_current)->key_next);
	}

	cfg->key_current = &(cfg->key_list);

	set_sigmask(SIGMASK_RST);
	return 0;

	fail:
		set_sigmask(SIGMASK_RST);
		list_free(&(cfg->key_list));
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

	cfg->key_current = &(cfg->key_list);

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
	// Either way, if close() fails, we're left in an undefined state.
	// 			-Luna
	list_free(&(cfg->key_list));
	free(cfg->buff);
	if(close(cfg->fd) != 0)
	{
		set_sigmask(SIGMASK_RST);
		return -1;
	}
	free(cfg);

	set_sigmask(SIGMASK_RST);
	return 0;
}

int config_index(CONFIG *restrict cfg, char *restrict name, char *restrict data_buff, unsigned int buff_size, unsigned int index)
{
	if(!set_sigmask(SIGMASK_SET))
		return -1;

	if(cfg == NULL || cfg->key_list == NULL)
	{
		*name = '\0';
		*data_buff = '\0';

		errno = EINVAL;
		set_sigmask(SIGMASK_RST);
		return -1;
	}

	k_list *tmp;

	tmp = list_get(index, cfg->key_list);
	if(tmp == NULL)
	{
		*name = '\0';
		*data_buff = '\0';

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
		data_buff[0] = '\0';

		errno = EINVAL;
		set_sigmask(SIGMASK_RST);
		return -1;
	}

	while(1)
	{
		if(*(cfg->key_current) == NULL || fast_cmp((*cfg->key_current)->name, name))
			break;

		cfg->key_current = &((*cfg->key_current)->key_next);
	}

	if(*(cfg->key_current) == NULL)
	{
		data_buff[0] = '\0';

		set_sigmask(SIGMASK_RST);
		return 0;
	}
	else
	{
		if(strlen((*cfg->key_current)->value) > (buff_size - 1))
		{
			memcpy(data_buff, (*cfg->key_current)->value, (buff_size));
			data_buff[buff_size - 1] = '\0';
			cfg->key_current = &((*cfg->key_current)->key_next);

			set_sigmask(SIGMASK_RST);
			return(strlen((*cfg->key_current)->value) - buff_size);
		}
		else
			memcpy(data_buff, (*cfg->key_current)->value, (strlen((*cfg->key_current)->value) + 1));

		cfg->key_current = &((*cfg->key_current)->key_next);

		set_sigmask(SIGMASK_RST);
		return 1;
	}
}

int config_next(CONFIG *restrict cfg, char *restrict name, char *restrict data_buff, unsigned int buff_size)
{
	if(!set_sigmask(SIGMASK_SET))
		return -1;

	if(cfg == NULL || cfg->key_current == NULL)
	{
		name[0] = '\0';
		data_buff[0] = '\0';

		errno = EINVAL;
		set_sigmask(SIGMASK_RST);
		return -1;
	}

	if(*(cfg->key_current) == NULL)
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
			cfg->key_current = &((*cfg->key_current)->key_next);

			set_sigmask(SIGMASK_RST);
			return(strlen((*cfg->key_current)->value) - buff_size);
		}
		else
			memcpy(data_buff, (*cfg->key_current)->value, (strlen((*cfg->key_current)->value) + 1));
	}

	cfg->key_current = &((*cfg->key_current)->key_next);

	set_sigmask(SIGMASK_RST);
	return 1;
}

void config_index_br(CONFIG *restrict cfg, char **restrict name, char **restrict data, unsigned int index)
{
	if(!set_sigmask(SIGMASK_SET))
		return;

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

	if(cfg == NULL || cfg->key_current == NULL || *(cfg->key_current) == NULL || name == NULL)
	{
		*data = NULL;

		errno = EINVAL;
		set_sigmask(SIGMASK_RST);
		return;
	}
	
	while(1)
	{
		if(*(cfg->key_current) == NULL || fast_cmp((*cfg->key_current)->name, name))
			break;

		cfg->key_current = &((*cfg->key_current)->key_next);
	}

	if(*(cfg->key_current) == NULL)
	{
		*data = NULL;

		set_sigmask(SIGMASK_RST);
		return;
	}
	else
	{
		*data = (*cfg->key_current)->value;
		cfg->key_current = &((*cfg->key_current)->key_next);

		set_sigmask(SIGMASK_RST);
		return;
	}
}

void config_next_br(CONFIG *restrict cfg, char **restrict name, char **restrict data)
{
	if(!set_sigmask(SIGMASK_SET))
		return;

	if(cfg == NULL || cfg->key_list == NULL)
	{
		*name = NULL;
		*data = NULL;

		errno = EINVAL;
		set_sigmask(SIGMASK_RST);
		return;
	}

	if(*(cfg->key_current) == NULL)
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
