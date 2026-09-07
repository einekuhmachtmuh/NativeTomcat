#include "common.h"

#define ITERS 100000
#define WARM 10000

static uint64_t t[ITERS], nocopy_times[ITERS];

int main(int argc, char **argv)
{
	JNIEnv *e;
	JavaVM *vm = start_vm(&e);
	jclass cls = (*e)->FindClass(e, "BridgeTarget");
	check_exc(e, "FindClass");
	jmethodID mid = (*e)->GetStaticMethodID(e, cls, "directDispatch",
		"(Ljava/nio/ByteBuffer;Ljava/nio/ByteBuffer;I)I");
	jmethodID nocopy_mid = (*e)->GetStaticMethodID(e, cls, "directNoCopy",
		"(Ljava/nio/ByteBuffer;Ljava/nio/ByteBuffer;I)I");
	check_exc(e, "GetStaticMethodID");

	const int N = argc > 1 ? atoi(argv[1]) : 1024;
	if (N <= 0 || N > 1048576)
		return 2;

	void *in = calloc(1, (size_t)N);
	void *out = calloc(1, (size_t)N);
	if (!in || !out)
		return 2;

	uint64_t a = now_ns();
	jobject ji = (*e)->NewDirectByteBuffer(e, in, N);
	jobject jo = (*e)->NewDirectByteBuffer(e, out, N);
	uint64_t b = now_ns();
	check_exc(e, "NewDirectByteBuffer");
	void *ai = (*e)->GetDirectBufferAddress(e, ji);
	void *ao = (*e)->GetDirectBufferAddress(e, jo);
	uint64_t d = now_ns();
	check_exc(e, "GetDirectBufferAddress");
	printf("DIRECT-BUFFER setup: NewDirectByteBuffer x2=%llu ns, GetDirectBufferAddress x2=%llu ns\n",
		(unsigned long long)(b - a), (unsigned long long)(d - b));
	if (ai != in || ao != out)
	{
		fprintf(stderr, "address mismatch\n");
		return 3;
	}

	for (int w = 0; w < WARM; w++)
	{
		(void)(*e)->CallStaticIntMethod(e, cls, mid, ji, jo, N);
		check_exc(e, "dispatch");
	}
	if (memcmp(in, out, (size_t)N) != 0)
	{
		fprintf(stderr, "direct buffer payload mismatch\n");
		return 4;
	}

	for (int i = 0; i < ITERS; i++)
	{
		uint64_t x = now_ns();
		(void)(*e)->CallStaticIntMethod(e, cls, nocopy_mid, ji, jo, N);
		uint64_t y = now_ns();
		check_exc(e, "nocopy");
		nocopy_times[i] = y - x;
		uint64_t p = now_ns();
		(void)(*e)->CallStaticIntMethod(e, cls, mid, ji, jo, N);
		uint64_t q = now_ns();
		check_exc(e, "dispatch");
		t[i] = q - p;
	}

	puts("DirectByteBuffer / zero-copy bridge");
	report("CallIntMethod no-copy", nocopy_times, ITERS);
	printf("CallIntMethod + %d-byte Java copy\n", N);
	report("direct Java copy", t, ITERS);
	puts("observed hot-path JNI API calls: 1");
	puts("bridge payload copy: 0 bytes");
	puts("Java target may still read/write native memory");

	free(in);
	free(out);
	(*e)->DeleteLocalRef(e, ji);
	(*e)->DeleteLocalRef(e, jo);
	(*vm)->DestroyJavaVM(vm);
	return 0;
}
