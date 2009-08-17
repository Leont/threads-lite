void global_init(pTHX);

mthread* mthread_alloc();
void mthread_finish(mthread*);

void S_thread_send(pTHX_ UV thread_id, message* message);
void S_thread_add_listener(pTHX_ UV talker, UV listener);
void S_send_listeners(pTHX_ mthread* thread, message* message);

#define thread_send(id, mess) S_thread_send(aTHX_ id, mess);
#define thread_add_listener(talker, listener) S_thread_add_listener(aTHX_ talker, listener)
#define send_listeners(thread, message) S_send_listeners(aTHX_ thread, message)
