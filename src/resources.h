void global_init(pTHX);

mthread* mthread_alloc(PerlInterpreter*);
void mthread_destroy(mthread*);
UV S_queue_alloc();

void S_thread_send(pTHX_ UV thread_id, message* message);
void S_queue_send(pTHX_ UV queue_id, message* message);
void S_queue_receive(pTHX_ UV queue_id, message* message);
bool S_queue_receive_nb(pTHX_ UV queue_id, message* message);
void thread_add_listener(pTHX, UV talker, UV listener);
void S_send_listeners(pTHX_ mthread* thread, message* message);

#define queue_alloc() S_queue_alloc(aTHX)
#define thread_send(id, mess) S_thread_send(aTHX_ id, mess);
#define queue_send(id, mess) S_queue_send(aTHX_ id, mess);
#define queue_receive(id, mess) S_queue_receive(aTHX_ id, mess);
#define queue_receive_nb(id, mess) S_queue_receive_nb(aTHX_ id, mess)
#define send_listeners(thread, message) S_send_listeners(aTHX_ thread, message)
