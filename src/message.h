enum message_type { EMPTY, STRING, PACKED, STORABLE };

typedef struct {
	enum message_type type;
	struct {
		char* ptr;
		STRLEN length;
	} string;
} message;

void S_message_clone(pTHX_ message* origin, message* clone);
#define message_clone(origin, clone) S_message_clone(aTHX_ origin, clone)

void S_message_to_stack(pTHX_ message*, U32 context);
#define message_to_stack(values, context) STMT_START { PUTBACK; S_message_to_stack(aTHX_ (values), context); SPAGAIN; } STMT_END

void S_message_to_array(pTHX_ message*, AV**);
#define message_to_array(values, context) S_message_to_array(aTHX_ (values), context)

void S_message_from_stack(pTHX_ message*);
#define message_from_stack_pushed(message) STMT_START { PUTBACK; S_message_from_stack(aTHX_ message); SPAGAIN; } STMT_END
#define message_from_stack(message, offset) STMT_START { PUSHMARK(offset); message_from_stack_pushed(message); } STMT_END

void S_message_store_value(pTHX_ message* message, SV* value);
#define message_store_value(message, value) S_message_store_value(aTHX_ message, value)

SV* S_message_load_value(pTHX_ message* message);
#define message_load_value(message) S_message_load_value(aTHX_ message)

