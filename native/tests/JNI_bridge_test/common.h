#ifndef JNI_BENCH_COMMON_H
#define JNI_BENCH_COMMON_H

#define _POSIX_C_SOURCE 200809L

#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t now_ns(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0)
	{
		perror("clock_gettime");
		exit(2);
	}
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a;
	uint64_t y = *(const uint64_t *)b;
	return x < y ? -1 : x > y ? 1 : 0;
}

static void report(const char *name, uint64_t *a, int n)
{
	qsort(a, (size_t)n, sizeof(*a), cmp_u64);
	unsigned long long sum = 0;
	for (int i = 0; i < n; i++)
		sum += (unsigned long long)a[i];
	printf("%-28s min=%llu ns p50=%llu ns p95=%llu ns p99=%llu ns avg=%llu ns\n", name,
		(unsigned long long)a[0], (unsigned long long)a[n / 2],
		(unsigned long long)a[(n * 95) / 100], (unsigned long long)a[(n * 99) / 100],
		sum / (unsigned long long)n);
}

static JavaVM *start_vm(JNIEnv **env_out)
{
	JavaVMOption opt[1];
	char cp[512];
	snprintf(cp, sizeof(cp), "-Djava.class.path=/tmp/jni-bridge-bench");
	opt[0].optionString = cp;
	JavaVMInitArgs args;
	memset(&args, 0, sizeof(args));
	args.version = JNI_VERSION_1_8;
	args.nOptions = 1;
	args.options = opt;
	args.ignoreUnrecognized = JNI_FALSE;
	JavaVM *vm = NULL;
	JNIEnv *env = NULL;
	jint rc = JNI_CreateJavaVM(&vm, (void **)&env, &args);
	if (rc != JNI_OK)
	{
		fprintf(stderr, "JNI_CreateJavaVM failed: %d\n", rc);
		exit(2);
	}
	*env_out = env;
	return vm;
}

static void check_exc(JNIEnv *env, const char *where)
{
	if ((*env)->ExceptionCheck(env))
	{
		fprintf(stderr, "Java exception at %s\n", where);
		(*env)->ExceptionDescribe(env);
		exit(3);
	}
}

#endif
