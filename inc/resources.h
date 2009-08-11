void global_init();

mthread* mthread_alloc();
void mthread_finish(mthread*);

void S_thread_send(pTHX_ UV thread_id, message* message);
AV* S_thread_join(pTHX_ UV thread_id);
void S_thread_detach(pTHX_ UV thread_id);

#define thread_send(id, mess) S_thread_send(aTHX_ id, mess);
#define thread_join(id) S_thread_join(aTHX_ id);
#define thread_detach(id) S_thread_detach(aTHX_ id);
