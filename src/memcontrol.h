/*
 * memcontrol.h
 *
 *  Created on: 05.11.2012
 *      Author: egor
 */

#ifndef MEMCONTROL_H_
#define MEMCONTROL_H_

void init_memory_hook (void);

void deinit_memory_hook (void);

void report_mem_usage_results(int nprocs);
void print_memtest_header(char *benchname);

#endif /* MEMCONTROL_H_ */
