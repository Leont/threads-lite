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

extern void S_create_push_threads(PerlInterpreter*, HV* options, SV* startup);
#define create_push_threads(options, startup) S_create_push_threads(aTHX, options, startup)
extern mthread* clone_thread(pTHX_ IV stack_size);
void store_self(pTHX, mthread*);
mthread* S_get_self(pTHX);
#define get_self() S_get_self(aTHX)
perl_mutex* get_shutdown_mutex();
