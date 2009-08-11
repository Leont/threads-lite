void global_init();

mthread* mthread_alloc();
void mthread_finish(mthread*);
bool thread_send(UV thread_id, message* message);
