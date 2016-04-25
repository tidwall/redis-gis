//#ifdef TILE38_TEST

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

//#endif
