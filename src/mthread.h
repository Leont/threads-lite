typedef struct mthread {
	UV thread_id;
	message_queue queue;
	perl_mutex lock;
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

extern mthread* create_thread(IV stack_size, IV linked_to);
void S_store_self(pTHX_ mthread*);
#define store_self(thread) S_store_self(aTHX_ thread)
