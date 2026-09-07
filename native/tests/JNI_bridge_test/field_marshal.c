#include "common.h"

#define ITERS 100000
#define WARM 10000

static uint64_t t[ITERS], reqobj[ITERS], strings[ITERS], body[ITERS];
static uint64_t fields[ITERS], dispatch_step[ITERS], response[ITERS];

int main(int argc, char **argv)
{
	JNIEnv *e;
	JavaVM *vm = start_vm(&e);
	jclass cls = (*e)->FindClass(e, "BridgeTarget");
	jclass reqc = (*e)->FindClass(e, "BridgeTarget$Request");
	jclass resc = (*e)->FindClass(e, "BridgeTarget$Response");
	check_exc(e, "FindClass");

	jmethodID req_ctor = (*e)->GetMethodID(e, reqc, "<init>", "()V");
	jmethodID dispatch = (*e)->GetStaticMethodID(e, cls, "fieldDispatch",
		"(LBridgeTarget$Request;)LBridgeTarget$Response;");
	jfieldID f_method = (*e)->GetFieldID(e, reqc, "method", "Ljava/lang/String;");
	jfieldID f_path = (*e)->GetFieldID(e, reqc, "path", "Ljava/lang/String;");
	jfieldID f_query = (*e)->GetFieldID(e, reqc, "query", "Ljava/lang/String;");
	jfieldID f_body = (*e)->GetFieldID(e, reqc, "body", "[B");
	jfieldID r_status = (*e)->GetFieldID(e, resc, "status", "I");
	jfieldID r_ct = (*e)->GetFieldID(e, resc, "contentType", "Ljava/lang/String;");
	jfieldID r_body = (*e)->GetFieldID(e, resc, "body", "[B");
	check_exc(e, "IDs");

	const char *method = "GET";
	const char *path = "/bench";
	const char *query = "x=1";
	const int N = argc > 1 ? atoi(argv[1]) : 1024;
	if (N <= 0 || N > 1048576)
		return 2;

	jbyte *payload = malloc((size_t)N);
	jbyte *out = malloc((size_t)N);
	if (!payload || !out)
		return 2;
	for (int i = 0; i < N; i++)
		payload[i] = (jbyte)i;

	for (int w = 0; w < WARM; w++)
	{
		jobject r = (*e)->NewObject(e, reqc, req_ctor);
		jstring jm = (*e)->NewStringUTF(e, method);
		jstring jp = (*e)->NewStringUTF(e, path);
		jstring jq = (*e)->NewStringUTF(e, query);
		jbyteArray jb = (*e)->NewByteArray(e, N);
		(*e)->SetByteArrayRegion(e, jb, 0, N, payload);
		(*e)->SetObjectField(e, r, f_method, jm);
		(*e)->SetObjectField(e, r, f_path, jp);
		(*e)->SetObjectField(e, r, f_query, jq);
		(*e)->SetObjectField(e, r, f_body, jb);
		jobject resp = (*e)->CallStaticObjectMethod(e, cls, dispatch, r);
		check_exc(e, "dispatch");
		(void)(*e)->GetIntField(e, resp, r_status);
		jstring ct = (jstring)(*e)->GetObjectField(e, resp, r_ct);
		char buf[64];
		(*e)->GetStringUTFRegion(e, ct, 0, 10, buf);
		jbyteArray rb = (jbyteArray)(*e)->GetObjectField(e, resp, r_body);
		(*e)->GetByteArrayRegion(e, rb, 0, N, out);
		(*e)->DeleteLocalRef(e, rb);
		(*e)->DeleteLocalRef(e, ct);
		(*e)->DeleteLocalRef(e, resp);
		(*e)->DeleteLocalRef(e, jb);
		(*e)->DeleteLocalRef(e, jq);
		(*e)->DeleteLocalRef(e, jp);
		(*e)->DeleteLocalRef(e, jm);
		(*e)->DeleteLocalRef(e, r);
	}

	{
		jobject r = (*e)->NewObject(e, reqc, req_ctor);
		jstring jm = (*e)->NewStringUTF(e, method);
		jstring jp = (*e)->NewStringUTF(e, path);
		jstring jq = (*e)->NewStringUTF(e, query);
		jbyteArray jb = (*e)->NewByteArray(e, N);
		(*e)->SetByteArrayRegion(e, jb, 0, N, payload);
		(*e)->SetObjectField(e, r, f_method, jm);
		(*e)->SetObjectField(e, r, f_path, jp);
		(*e)->SetObjectField(e, r, f_query, jq);
		(*e)->SetObjectField(e, r, f_body, jb);
		jobject resp = (*e)->CallStaticObjectMethod(e, cls, dispatch, r);
		check_exc(e, "validation dispatch");
		jbyteArray rb = (jbyteArray)(*e)->GetObjectField(e, resp, r_body);
		(*e)->GetByteArrayRegion(e, rb, 0, N, out);
		if (memcmp(payload, out, (size_t)N) != 0)
		{
			fprintf(stderr, "field marshal payload mismatch\n");
			return 4;
		}
		(*e)->DeleteLocalRef(e, rb);
		(*e)->DeleteLocalRef(e, resp);
		(*e)->DeleteLocalRef(e, jb);
		(*e)->DeleteLocalRef(e, jq);
		(*e)->DeleteLocalRef(e, jp);
		(*e)->DeleteLocalRef(e, jm);
		(*e)->DeleteLocalRef(e, r);
	}

	for (int i = 0; i < ITERS; i++)
	{
		uint64_t a = now_ns();
		jobject r = (*e)->NewObject(e, reqc, req_ctor);
		uint64_t q1 = now_ns();
		jstring jm = (*e)->NewStringUTF(e, method);
		jstring jp = (*e)->NewStringUTF(e, path);
		jstring jq = (*e)->NewStringUTF(e, query);
		uint64_t q2 = now_ns();
		jbyteArray jb = (*e)->NewByteArray(e, N);
		(*e)->SetByteArrayRegion(e, jb, 0, N, payload);
		uint64_t q3 = now_ns();
		(*e)->SetObjectField(e, r, f_method, jm);
		(*e)->SetObjectField(e, r, f_path, jp);
		(*e)->SetObjectField(e, r, f_query, jq);
		(*e)->SetObjectField(e, r, f_body, jb);
		uint64_t b = now_ns();
		jobject resp = (*e)->CallStaticObjectMethod(e, cls, dispatch, r);
		check_exc(e, "dispatch");
		uint64_t c = now_ns();
		(void)(*e)->GetIntField(e, resp, r_status);
		jstring ct = (jstring)(*e)->GetObjectField(e, resp, r_ct);
		char buf[64];
		(*e)->GetStringUTFRegion(e, ct, 0, 10, buf);
		jbyteArray rb = (jbyteArray)(*e)->GetObjectField(e, resp, r_body);
		(*e)->GetByteArrayRegion(e, rb, 0, N, out);
		uint64_t d = now_ns();
		t[i] = d - a;
		reqobj[i] = q1 - a;
		strings[i] = q2 - q1;
		body[i] = q3 - q2;
		fields[i] = b - q3;
		dispatch_step[i] = c - b;
		response[i] = d - c;
		(*e)->DeleteLocalRef(e, rb);
		(*e)->DeleteLocalRef(e, ct);
		(*e)->DeleteLocalRef(e, resp);
		(*e)->DeleteLocalRef(e, jb);
		(*e)->DeleteLocalRef(e, jq);
		(*e)->DeleteLocalRef(e, jp);
		(*e)->DeleteLocalRef(e, jm);
		(*e)->DeleteLocalRef(e, r);
	}

	puts("FIELD-MARSHAL");
	report("full bridge/request", t, ITERS);
	report("NewObject(Request)", reqobj, ITERS);
	report("NewStringUTF x3", strings, ITERS);
	report("NewByteArray + SetRegion", body, ITERS);
	report("SetObjectField x4", fields, ITERS);
	report("CallObjectMethod target", dispatch_step, ITERS);
	report("response field extraction", response, ITERS);
	puts("observed hot-path JNI API calls (excluding cleanup): 16");
	puts("bridge-created Java objects per request in this target: Request + 3 Strings + byte[] + Response = 6");

	free(payload);
	free(out);
	(*vm)->DestroyJavaVM(vm);
	return 0;
}
