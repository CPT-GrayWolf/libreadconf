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
#ifndef LIBREADCONF_H
#define LIBREADCONF_H

#define CONFIG_MAX_KEY 64

#ifdef __cplusplus
extern "C"{
#endif

typedef struct config CONFIG;

extern CONFIG *config_open(const char *path);
extern CONFIG *config_fdopen(int fd);
extern CONFIG *config_reopen(const char * path, CONFIG *cfg);
extern int config_read(CONFIG *cfg);
extern int config_close(CONFIG *cfg);
extern int config_rewind(CONFIG *cfg);

extern int config_index(CONFIG *cfg, char *name, char *data_buff, unsigned int buff_size, unsigned int index);
extern int config_search(CONFIG *cfg, const char *name, char *data_buff, unsigned int buff_size);
extern int config_next(CONFIG *cfg, char *name, char *data_buff, unsigned int buff_size);

extern int config_index_br(CONFIG *cfg, char **name, char **data, unsigned int index);
extern int config_search_br(CONFIG *cfg, const char *name, char **data);
extern int config_next_br(CONFIG *cfg, char **name, char **data);
  
#ifdef __cplusplus
}
#endif

#endif
