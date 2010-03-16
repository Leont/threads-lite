void global_init(pTHX);

mthread* mthread_alloc(PerlInterpreter*);
UV queue_alloc();

void S_thread_send(pTHX_ UV thread_id, message* message);
void thread_add_listener(pTHX, UV talker, UV listener);
void S_send_listeners(pTHX_ mthread* thread, message* message);

#define thread_send(id, mess) S_thread_send(aTHX_ id, mess);
#define send_listeners(thread, message) S_send_listeners(aTHX_ thread, message)
