typedef struct mthread {
	PerlInterpreter* interp;
	perl_mutex lock;
	message_queue queue;
	UV id;
#ifdef WIN32
	DWORD  thr;                 /* OS's idea if thread id */
	HANDLE handle;              /* OS's waitable handle */
#else
	pthread_t thr;              /* OS's handle for the thread */
	sigset_t initial_sigmask;   /* Thread wakes up with signals blocked */
#endif
	struct {
		IV* list;
		UV head;
		UV alloc;
	} listeners;
} mthread;

extern mthread* S_create_thread(PerlInterpreter*, SV* startup);
#define create_thread(startup) S_create_thread(aTHX_ startup)
extern void S_create_push_threads(PerlInterpreter*, SV* options, SV* startup);
#define create_push_threads(options, startup) S_create_push_threads(aTHX, options, startup)
extern mthread* clone_thread(pTHX_ IV stack_size);
void S_store_self(pTHX_ mthread*);
#define store_self(thread) S_store_self(aTHX_ thread)
mthread* S_get_self(pTHX);
#define get_self() S_get_self(aTHX)
