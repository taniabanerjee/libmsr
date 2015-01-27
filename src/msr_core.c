/* msr_core.c
 *
 * Copyright (c) 2011, 2012, 2013, 2014, 2015 by Lawrence Livermore National Security, LLC. LLNL-CODE-645430 
 * Produced at the Lawrence Livermore National Laboratory.
 * Written by Kathleen Shoga and Barry Rountree (shoga1|rountree@llnl.gov).
 * All rights reserved.
 *
 * This file is part of libmsr.
 *
 * libmsr is free software: you can redistribute it and/or 
 * modify it under the terms of the GNU Lesser General Public 
 * License as published by the Free Software Foundation, either 
 * version 3 of the License, or (at your option) any
 * later version.
 *
 * libmsr is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public 
 * License along
 * with libmsr. If not, see <http://www.gnu.org/licenses/>.
 *
 * This material is based upon work supported by the U.S. Department
 * of Energy's Lawrence Livermore National Laboratory. Office of 
 * Science, under Award number DE-AC52-07NA27344.
 *
 */

// Necessary for pread & pwrite.
#define _XOPEN_SOURCE 500

#include <stdio.h>	
#include <stdlib.h>
#include <unistd.h>	
#include <sys/stat.h> 	
#include <fcntl.h>	
#include <stdint.h>	
#include <errno.h>
#include <assert.h>
#include "msr_core.h"
#include "msr_counters.h"

#define LIBMSR_DEBUG_TAG "LIBMSR"
//#define LIBMSR_DEBUG     1
static int core_fd[NUM_DEVS];

/*********************************************************************************
*
* Helper functions for init_msr().
*
*********************************************************************************/

static int 
stat_check(char* filename, struct stat *statbuf){

	int retVal = stat(filename, statbuf);

	if (retVal == -1) {
		snprintf(filename, 1024, "%s %s::%d  stat failed on %s.\n", 
			getenv("HOSTNAME"),__FILE__, __LINE__, filename);
		perror(filename);
	}	
	return retVal;
}

static int 
rw_check(char* filename, struct stat *statbuf){

	if( !(statbuf->st_mode & S_IRUSR) || !(statbuf->st_mode & S_IWUSR) ){
		snprintf(filename, 1024, "%s %s::%d  Read/write permissions denied on %s.\n", 
			getenv("HOSTNAME"),__FILE__, __LINE__, filename);
		perror(filename);
		return -1;
	}
	return 0;
}

static int 
safe_open(char* filename, int close_on_success){
	int fd =  open( filename, O_RDWR );
	if(fd == -1){
		snprintf(filename, 1024, "%s %s::%d  Error opening %s.\n", 
			getenv("HOSTNAME"),__FILE__, __LINE__, filename);
		perror(filename);
	}else if (close_on_success){
		close( fd );	
	}
	return fd;
}

/*********************************************************************************
*
* init_msr()
*
*********************************************************************************/
int
init_msr(){
	int dev_idx;
	char filename[1025];
	char* filename_base[4] = {"msr_dev", "msr_safe", "msr", 0};
	static int initialized = 0;
	int fn_idx=0, skip_remainder=0;
	struct stat statbuf;

	if( initialized ){
		return 0;
	}

	fprintf(stderr, "%s Initializing %d device(s).\n", getenv("HOSTNAME"),NUM_DEVS);

	/*	Determining initialization errors has been problematic.  This algorithm
	 *	requires that all NUM_DEVS msr* files be present, writable and openable.
	 *	If any single file has a problem, the algorithm iterates to the next
	 *	filename_base and starts over.  We look for "msr_dev" files first, then 
	 *	"msr_safe" files provided by the msr-safe kernel module, and finally the
	 *	"msr" files provided by the stock msr kernel module.
	 *
	 *	Note:  if using the stock msr module, recent kernels do a capabilities
	 *	check.  Running as a non-root account without the appropriate cababilites
	 *	also leads to an error.
	 *
	 * 	Note:  we assume a working configuration provides rw permissions on the
	 * 	msr files.  There may be use cases where a read-only initialization may
	 * 	be useful, but we do not do that here (yet).
	 *
	 * 	Note:  we might also want to allow the user to specify a particular 
	 * 	filename_base.  That also is not handled (yet).
	 */

	do{	// iterate over filenames until we find one that works.

		skip_remainder = 0;	// Reset.

		for (dev_idx=0; dev_idx < NUM_DEVS; dev_idx++){

			snprintf(filename, 1024, "/dev/cpu/%d/%s", dev_idx, filename_base[fn_idx]);

			skip_remainder = stat_check(filename, &statbuf);
			
			if( !skip_remainder ) {
				skip_remainder = rw_check(filename, &statbuf);
			}

			if( !skip_remainder ){
				skip_remainder = (safe_open(filename, 1) == -1); // close on success 
			}

			if(skip_remainder){
				break;	// break out of the for loop, as we found one that didn't work.
			}
		}

		if(skip_remainder){
			fn_idx++;
			continue;	// continue to the next potential filename_base
		}else{
			break;		// we have a winner...
		}
	}while( filename_base[fn_idx] );

	if(skip_remainder){		
		// If we have gotten to this point there is no particular reason to continue.
		fprintf(stderr, "No correctly-configured msr files found.  Exiting.\n");
		exit(-1);
	}else{

		// Finally, now that we've passed the sanity checks, initialize the file descriptors.
		for (dev_idx=0; dev_idx < NUM_DEVS; dev_idx++){

			snprintf(filename, 1024, "/dev/cpu/%d/%s", dev_idx, filename_base[fn_idx]);
			core_fd[dev_idx] = safe_open( filename, 0 );  // keep open on success

			if(core_fd[dev_idx] == -1){
				// This open ostensibly worked just moments ago.  Should never get here.
				fprintf(stderr, "%s passed its open check and subsequently failed to open.  Exiting.", filename);
				exit(-1);
			}
		}
		initialized = 1;
	}
	return 0;
}

/*********************************************************************************
*
* finalize_msr()
*
*********************************************************************************/
void 
finalize_msr(){
	int dev_idx, rc;
	char filename[1025];
	for (dev_idx=0; dev_idx < NUM_DEVS; dev_idx++){
		if(core_fd[dev_idx]){
			rc = close(core_fd[dev_idx]);
			if( rc != 0 ){
				snprintf(filename, 1024, "%s %s::%d  Error closing file /dev/cpu/%d/msr\n", 
						getenv("HOSTNAME"),__FILE__, __LINE__, dev_idx); 
				perror(filename);
			}else{
				core_fd[dev_idx] = 0;
			}
		}
	}
}

/*********************************************************************************
 *
 * 
 * write_msr_by_coord( int socket, int core, int thread, off_t msr, uint64_t  val );
 * read_msr_by_coord(  int socket, int core, int thread, off_t msr, uint64_t *val );
 *
 * write_all_sockets(   off_t msr, uint64_t  val );
 * write_all_cores(     off_t msr, uint64_t  val );
 * write_all_threads(   off_t msr, uint64_t  val );
 *
 * write_all_sockets_v( off_t msr, uint64_t *val );
 * write_all_cores_v(   off_t msr, uint64_t *val );
 * write_all_threads_v( off_t msr, uint64_t *val );
 *
 * read_all_sockets(    off_t msr, uint64_t *val );
 * read_all_cores(      off_t msr, uint64_t *val );
 * read_all_threads(    off_t msr, uint64_t *val );
 *
 * read_msr_by_idx(  int dev_idx, off_t msr, uint64_t *val );
 * write_msr_by_idx( int dev_idx, off_t msr, uint64_t  val );
 *
 *********************************************************************************/
void
write_msr_by_coord( int socket, int core, int thread, off_t msr, uint64_t  val ){
#ifdef LIBMSR_DEBUG
	fprintf(stderr, "%s %s %s::%d (write_msr_by_coord) socket=%d core=%d thread=%d msr=%lu (0x%lx) val=%lu\n", getenv("HOSTNAME"),LIBMSR_DEBUG_TAG, __FILE__, __LINE__, socket, core, thread, msr, msr, val);
#endif
	write_msr_by_idx( socket * NUM_CORES_PER_SOCKET + core * NUM_THREADS_PER_CORE + thread, msr, val );
}

void
read_msr_by_coord(  int socket, int core, int thread, off_t msr, uint64_t *val ){
#ifdef LIBMSR_DEBUG
	fprintf(stderr, "%s %s %s::%d (read_msr_by_coord) socket=%d core=%d thread=%d msr=%lu (0x%lx)\n", getenv("HOSTNAME"),LIBMSR_DEBUG_TAG, __FILE__, __LINE__, socket, core, thread, msr, msr);
#endif
	assert(socket < NUM_SOCKETS);
	assert(core   < NUM_CORES_PER_SOCKET);
	assert(thread < NUM_THREADS_PER_CORE);
	assert(socket >= 0 );
	assert(core   >= 0 );
	assert(thread >= 0 );
	read_msr_by_idx( socket * NUM_CORES_PER_SOCKET + core * NUM_THREADS_PER_CORE + thread, msr, val );
}

void
write_all_sockets(   off_t msr, uint64_t  val ){
	int dev_idx;
#ifdef LIBMSR_DEBUG
	fprintf(stderr, "%s %s %s::%d (write_all_sockets) msr=%lu (0x%lx)\n", getenv("HOSTNAME"),LIBMSR_DEBUG_TAG, __FILE__, __LINE__, msr, msr);
#endif
	for(dev_idx=0; dev_idx<NUM_DEVS; dev_idx += NUM_CORES_PER_SOCKET * NUM_THREADS_PER_CORE ){
		write_msr_by_idx( dev_idx, msr, val );
	}
}

void
write_all_cores(     off_t msr, uint64_t  val ){
	int dev_idx;
#ifdef LIBMSR_DEBUG
	fprintf(stderr, "%s %s %s::%d (write_all_cores) msr=%lu (0x%lx)\n", getenv("HOSTNAME"),LIBMSR_DEBUG_TAG, __FILE__, __LINE__, msr, msr);
#endif
	for(dev_idx=0; dev_idx<NUM_DEVS; dev_idx += NUM_THREADS_PER_CORE ){
		write_msr_by_idx( dev_idx, msr, val );
	}
}

void
write_all_threads(   off_t msr, uint64_t  val ){
	int dev_idx;
#ifdef LIBMSR_DEBUG
	fprintf(stderr, "%s %s %s::%d (write_all_threads) msr=%lu (0x%lx)\n", getenv("HOSTNAME"),LIBMSR_DEBUG_TAG, __FILE__, __LINE__, msr, msr);
#endif
	for(dev_idx=0; dev_idx<NUM_DEVS; dev_idx++){
		write_msr_by_idx( dev_idx, msr, val );
	}
}

void
write_all_sockets_v( off_t msr, uint64_t *val ){
	int dev_idx, val_idx;
#ifdef LIBMSR_DEBUG
	fprintf(stderr, "%s %s %s::%d (write_all_sockets_v) msr=%lu (0x%lx)\n", getenv("HOSTNAME"),LIBMSR_DEBUG_TAG, __FILE__, __LINE__, msr, msr);
#endif
	for(dev_idx=0, val_idx=0; dev_idx<NUM_DEVS; dev_idx += NUM_CORES_PER_SOCKET*NUM_THREADS_PER_CORE, val_idx++ ){
		write_msr_by_idx( dev_idx, msr, val[val_idx] );
	}
}

void
write_all_cores_v(   off_t msr, uint64_t *val ){
	int dev_idx, val_idx;
#ifdef LIBMSR_DEBUG
	fprintf(stderr, "%s %s %s::%d (write_all_cores_v) msr=%lu (0x%lx)\n", getenv("HOSTNAME"),LIBMSR_DEBUG_TAG, __FILE__, __LINE__, msr, msr);
#endif
	for(dev_idx=0, val_idx=0; dev_idx<NUM_DEVS; dev_idx += NUM_THREADS_PER_CORE, val_idx++ ){
		write_msr_by_idx( dev_idx, msr, val[val_idx] );
	}
}

void
write_all_threads_v( off_t msr, uint64_t *val ){
	int dev_idx, val_idx;
#ifdef LIBMSR_DEBUG
	fprintf(stderr, "%s %s %s::%d (write_all_threads_v) msr=%lu (0x%lx)\n", getenv("HOSTNAME"),LIBMSR_DEBUG_TAG, __FILE__, __LINE__, msr, msr);
#endif
	for(dev_idx=0, val_idx=0; dev_idx<NUM_DEVS; dev_idx++, val_idx++ ){
		write_msr_by_idx( dev_idx, msr, val[val_idx] );
	}
}


void
read_all_sockets(    off_t msr, uint64_t *val ){
	int dev_idx, val_idx;
#ifdef LIBMSR_DEBUG
	fprintf(stderr, "%s %s %s::%d (read_all_sockets) msr=%lu (0x%lx)\n", getenv("HOSTNAME"),LIBMSR_DEBUG_TAG, __FILE__, __LINE__, msr, msr);
#endif
	for(dev_idx=0, val_idx=0; dev_idx<NUM_DEVS; dev_idx += NUM_CORES_PER_SOCKET*NUM_THREADS_PER_CORE, val_idx++ ){
		read_msr_by_idx( dev_idx, msr, &val[val_idx] );
	}
}

void
read_all_cores(      off_t msr, uint64_t *val ){
	int dev_idx, val_idx;
#ifdef LIBMSR_DEBUG
	fprintf(stderr, "%s %s %s::%d (read_all_cores) msr=%lu (0x%lx)\n", getenv("HOSTNAME"),LIBMSR_DEBUG_TAG, __FILE__, __LINE__, msr, msr);
#endif
	for(dev_idx=0, val_idx=0; dev_idx<NUM_DEVS; dev_idx += NUM_THREADS_PER_CORE, val_idx++ ){
		read_msr_by_idx( dev_idx, msr, &val[val_idx] );
	}
}

void
read_all_threads(    off_t msr, uint64_t *val ){
	int dev_idx, val_idx;
#ifdef LIBMSR_DEBUG
	fprintf(stderr, "%s %s %s::%d (read_all_threads) msr=%lu (0x%lx)\n", getenv("HOSTNAME"),LIBMSR_DEBUG_TAG, __FILE__, __LINE__, msr, msr);
#endif
	for(dev_idx=0, val_idx=0; dev_idx<NUM_DEVS; dev_idx++, val_idx++ ){
		read_msr_by_idx( dev_idx, msr, &val[val_idx] );
	}
}

void
read_msr_by_idx(  int dev_idx, off_t msr, uint64_t *val ){
	int rc;
	char error_msg[1025];
#ifdef LIBMSR_DEBUG
	fprintf(stderr, "%s %s %s::%d (read_msr_by_idx) msr=%lu (0x%lx)\n", getenv("HOSTNAME"),LIBMSR_DEBUG_TAG, __FILE__, __LINE__, msr, msr);
#endif
	rc = pread( core_fd[dev_idx], (void*)val, (size_t)sizeof(uint64_t), msr );
	if( rc != sizeof(uint64_t) ){
		snprintf( error_msg, 1024, "%s %s::%d  pread returned %d.  core_fd[%d]=%d, msr=%ld (0x%lx).  errno=%d\n", 
				getenv("HOSTNAME"),__FILE__, __LINE__, rc, dev_idx, core_fd[dev_idx], msr, msr, errno );
		perror(error_msg);
	}
}

void
write_msr_by_idx( int dev_idx, off_t msr, uint64_t  val ){
	int rc;
	char error_msg[1025];
#ifdef LIBMSR_DEBUG
	fprintf(stderr, "%s %s %s::%d (write_msr_by_idx) msr=%lu (0x%lx)\n", getenv("HOSTNAME"),LIBMSR_DEBUG_TAG, __FILE__, __LINE__, msr, msr);
#endif
	rc = pwrite( core_fd[dev_idx], &val, (size_t)sizeof(uint64_t), msr );
	if( rc != sizeof(uint64_t) ){
		snprintf( error_msg, 1024, "%s %s::%d  pwrite returned %d.  core_fd[%d]=%d, msr=%ld (0x%lx).  errno=%d\n", 
				getenv("HOSTNAME"),__FILE__, __LINE__, rc, dev_idx, core_fd[dev_idx], msr, msr, errno );
		perror(error_msg);
	}
}

