typedef struct mthread {
	UV thread_id;
	enum { INITING, RUNNING, DETACHED, FINISHED } status;
	message_queue queue;
#ifdef WIN32
	DWORD  thr;                 /* OS's idea if thread id */
	HANDLE handle;              /* OS's waitable handle */
#else
	pthread_t thr;              /* OS's handle for the thread */
	sigset_t initial_sigmask;   /* Thread wakes up with signals blocked */
#endif
} mthread;

extern mthread* create_thread(IV stack_size);
