/*
 * Copyright (c) 2016, Josh Baker <joshbaker77@gmail.com>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Redis nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

int test_Geom();
int test_GeomZ();
int test_GeomZM();

typedef struct test{
	char *name;
	int (*test)();
} test;

test tests[] = {
	{ "geom", test_Geom },
	{ "geomZ", test_GeomZ },
	{ "geomZM", test_GeomZM },
};

void sig_handler(int sig) {
	printf("\x1b[0m\n");
	exit(-1);
}

int main(int argc, const char **argv) {
	signal(SIGSEGV, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGHUP, sig_handler);
	signal(SIGINT, sig_handler);

	const char *run = getenv("RUNTEST");
	if (run == NULL){
		run = "";
	}

	for (int i=1;i<argc;i++){
		const char *arg = argv[i];
		if (strcmp(arg, "-r") == 0 || strcmp(arg, "-run") == 0 || strcmp(arg, "--run") == 0){
			if (i+1==argc){
				fprintf(stderr, "argument '%s' requires a value\n", arg);
				exit(-1);	
			}
			arg = argv[i+1];
			i++;
			run = arg;
		} else {
			fprintf(stderr, "unknown argument '%s'\n", arg);
			exit(-1);
		}
	}

	for (int i=0;i<sizeof(tests)/sizeof(test);i++){
		test t = tests[i];

		if (!(strlen(run) == 0 || strcmp(run, t.name) == 0)){
			continue;
		}
		char label[50];
		sprintf(label, "    Test %s ... ", t.name); 
		fprintf(stdout, "%-25s", label);
		fflush(stdout);
		if (!t.test()){
			printf("\x1b[31m[failed]\x1b[0m\n");
		} else{
			printf("\x1b[32m[ok]\x1b[0m\n");
		}
	}
}
