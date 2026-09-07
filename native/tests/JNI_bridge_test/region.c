#include "common.h"

#define ITERS 100000
#define WARM 10000

static uint64_t t[ITERS], step[ITERS], inreg[ITERS], outreg[ITERS];

int main(int argc, char **argv)
{
	JNIEnv *e;
	JavaVM *vm = start_vm(&e);
	jclass cls = (*e)->FindClass(e, "BridgeTarget");
	check_exc(e, "FindClass");
	jmethodID mid = (*e)->GetStaticMethodID(e, cls, "regionDispatch", "([B[BI)I");
	check_exc(e, "GetStaticMethodID");

	const int N = argc > 1 ? atoi(argv[1]) : 1024;
	if (N <= 0 || N > 1048576)
		return 2;

	jbyte *in = malloc((size_t)N);
	jbyte *out = malloc((size_t)N);
	if (!in || !out)
		return 2;
	for (int i = 0; i < N; i++)
		in[i] = (jbyte)i;

	jbyteArray ji = (*e)->NewByteArray(e, N);
	jbyteArray jo = (*e)->NewByteArray(e, N);
	check_exc(e, "arrays");

	for (int w = 0; w < WARM; w++)
	{
		(*e)->SetByteArrayRegion(e, ji, 0, N, in);
		(void)(*e)->CallStaticIntMethod(e, cls, mid, ji, jo, N);
		check_exc(e, "dispatch");
		(*e)->GetByteArrayRegion(e, jo, 0, N, out);
	}
	if (memcmp(in, out, (size_t)N) != 0)
	{
		fprintf(stderr, "region payload mismatch\n");
		return 4;
	}

	for (int i = 0; i < ITERS; i++)
	{
		uint64_t a = now_ns();
		(*e)->SetByteArrayRegion(e, ji, 0, N, in);
		uint64_t b = now_ns();
		jint m = (*e)->CallStaticIntMethod(e, cls, mid, ji, jo, N);
		uint64_t c = now_ns();
		check_exc(e, "dispatch");
		(*e)->GetByteArrayRegion(e, jo, 0, m, out);
		uint64_t d = now_ns();
		t[i] = d - a;
		inreg[i] = b - a;
		step[i] = c - b;
		outreg[i] = d - c;
	}

	puts("BYTE[] + Region");
	report("full bridge/request", t, ITERS);
	report("SetByteArrayRegion(N)", inreg, ITERS);
	report("CallIntMethod target", step, ITERS);
	report("GetByteArrayRegion(M)", outreg, ITERS);
	puts("observed hot-path JNI API calls: 3");
	puts("reusable Java arrays: yes");
	puts("bridge payload copies: N + M bytes");

	(*e)->DeleteLocalRef(e, ji);
	(*e)->DeleteLocalRef(e, jo);
	free(in);
	free(out);
	(*vm)->DestroyJavaVM(vm);
	return 0;
}
