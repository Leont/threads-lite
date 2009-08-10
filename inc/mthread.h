typedef struct mthread {
	message_queue* queue;
	enum { INITING, RUNNING, FINISHED } status;
#ifdef WIN32
	DWORD  thr;                 /* OS's idea if thread id */
	HANDLE handle;              /* OS's waitable handle */
#else
	pthread_t thr;              /* OS's handle for the thread */
	sigset_t initial_sigmask;   /* Thread wakes up with signals blocked */
#endif
} mthread;

extern mthread* create_thread(IV stack_size);
