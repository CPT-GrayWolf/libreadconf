## The  libreadconf Library
The libreadconf library provides the basic functionality needed to work with classic UNIX style configuration files, while also providing enough functionality to allow more complex behavior.

It provides a simple interface that should be familiar to anyone who have experience with stdio and POSIX style functions.

## Installation
### Building:
The build process can now be optimally configured for your system automatically using the included configuration script:

	# ./configure
This script will write the relevant information to the "setup.mk" file in the build directory. If the script is run in a directory other than the one in which it is located, it will generate copies of the appropriate makefiles, preconfigured to allow the library to be built and installed from that directory with out the need to specify the original location. 

The library may be built using 'make', in the same way as many other programs:

	# make
> **NOTES:**
> **Configure**
> The included configuration script makes some basic assumptions and choices based of the information it gathers on your system.
> Much of this can be overridden by setting the appropriate variable beforehand and specifying the corresponding argument.
>
> *-d* - Can be used to regenerate the default "setup.mk". This overrides any other options.
> *-l* - If the LIBDIR variable is set, then it will be set as the target installation directory for the library.
> *-i* - If the INCLUDEDIR variable is set, then it will be  set as the target installation directory for that library's header file.
> *-c* - If the CC variable is set, then it will be used as the target compiler.
>
> The configuration script will test for available compilers in the following order: clang, gcc, cc.
> Use *-c* as specified above to override this.
> 
> **Make**
> The library may be built including debugging symbols by using the "debug" target. All other options remain the same.
>
> By default libreadconf uses some basic pthread calls to unsure that its signal handling is threadsafe.
> If you want to disable this for some reason, set CFLAGS to "-D NO_PTHREAD".
>
> If you want to add custom compiler flags, but compile with pthread options enabled, be sure to include the "-pthread" option in your flags, or the library will not compile.
>
> By default libreadconf has a minimum buffer size. 
> If the block size for a file is below this size, the block size will be ignored, and the minimum buffer size is used. This may be beneficial in some case, but may waste resources in others.
> To disable minimum buffer size, include the "-D NO_MIN_BUFF" option in CFLAGS.

If all goes well, you should see a nice clean compilation, free of any errors or warnings.

### Installing:
The library can, again, be installed using 'make':

	# make install
This will install the library, header, and all its included manpages, for your convenience.
> NOTES:
> By default, the included Makefile assumes the .so file will be installed in "/lib". If you need/wish it to be installed elsewhere, this can be set using the TARGETDIR option.
> Similarly if you wish the header file to be installed somewhere other than "/usr/include", This may be set using the INCLUDEDIR option.

### Cleanup:
Once you are finished building and installing libreadconf, the Makefile provides you with an option to clean up any files left over from the build process:

	# make clean
This step is not required.

### Uninstallation:
For your convenience we include a "remove" target for make that, so long as it is run using the same environment as the build and installation steps, will remove all the files installed by "make install":

	# make remove
This automates the removal of the library, headers, and manpages, all in one step, removing the need to manually fine where they were installed. (You're welcome.)
>Note: "make clean" does not affect this target.

## Usage
The libreadconf library provides seven function to open, read, and look through configuration files.

The **CONFIG** type is used to handle configuration files, and functions similarly to the stdio **FILE** type.

The following is only an overview. The manpages provided with the library give more in-depth descriptions of the library and its functions (Also see the included examples).

All functions, constants, and datatypes are provided by the <libreadconf.h> header.

* **CONFIG \*config_open(const char \*path)**  
This function opens a file and returns a CONFIG associated with it.

* **CONFIG \*config_fdopen(int fd)**  
The same as config_open(), but takes a file descriptor, rather than a pathname.

* **CONFIG \*config_reopen(const char \* path, CONFIG \*cfg)**  
Re-opens the provided CONFIG and associates it with a new file.

* **int config_read(CONFIG \*cfg)**  
Read all the data from an open CONFIG, stores it in a keylist, and prepares it to be searched.

* **int config_close(CONFIG \*cfg)**  
Closes an open CONFIG and frees any memory associated with it.

* **int config_rewind(CONFIG \*cfg)**  
Rewind the keylist associated with a CONFIG.

* **config_index(CONFIG \*cfg, char \*name, char \*data_buff, unsigned int buff_size, unsigned int index)**  
Look through the keylist of a CONFIG as you would an array, without changing the keylist position.

* **config_search(CONFIG \*cfg, const char \*name, char \*data_buff, unsigned int buff_size)**  
Search for the next occurrence of an item in the keylist of a CONFIG.

* **config_next(CONFIG \*cfg, char \*name, char \*data_buff, unsigned int buff_size)**  
Get the next item from the keylist of a CONFIG.

The config_index(), config_search(), and config_next functions all have by-reference counterparts that allow a programmer to access the memory that libreadconf allocates directly, rather than worrying about memcpy()s and buffer lengths. However there are caveats to doing this (see the libreadconf(3) manpage).

## Notes
### Signals:
The libreadconf library's functions all block SIGHUP when executing. This eliminates the need to worry about blocking it yourself, if your program relies on SIGHUP to indicate that it must reload its configuration.

### Multithreading:
While libreadconf is thread aware enough that it should function normally (so long as only one thread is handling any given CONFIG at a time), the library has not yet been tested for thread safety. (Testers welcome!)

Also be aware that, while a specific thread running a libreadconf function will block SIGHUP, *this will not stop other threads from recieving the signal!*
