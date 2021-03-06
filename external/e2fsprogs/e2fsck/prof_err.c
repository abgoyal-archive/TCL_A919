
#include <stdlib.h>

static const char * const text[] = {
	"Profile version 0.0",
	"Bad magic value in profile_node",
	"Profile section not found",
	"Profile relation not found",
		"Attempt to add a relation to node which is not a section",
		"A profile section header has a non-zero value",
	"Bad linked list in profile structures",
	"Bad group level in profile strctures",
		"Bad parent pointer in profile strctures",
	"Bad magic value in profile iterator",
	"Can't set value on section node",
	"Invalid argument passed to profile library",
	"Attempt to modify read-only profile",
	"Profile section header not at top level",
	"Syntax error in profile section header",
	"Syntax error in profile relation",
	"Extra closing brace in profile",
	"Missing open brace in profile",
	"Bad magic value in profile_t",
	"Bad magic value in profile_section_t",
		"Iteration through all top level section not supported",
	"Invalid profile_section object",
	"No more sections",
	"Bad nameset passed to query routine",
	"No profile file open",
	"Bad magic value in profile_file_t",
	"Couldn't open profile file",
	"Section already exists",
	"Invalid boolean value",
	"Invalid integer value",
	"Bad magic value in profile_file_data_t",
    0
};

struct error_table {
    char const * const * msgs;
    long base;
    int n_msgs;
};
struct et_list {
    struct et_list *next;
    const struct error_table * table;
};
extern struct et_list *_et_list;

const struct error_table et_prof_error_table = { text, -1429577728L, 31 };

static struct et_list link = { 0, 0 };

void initialize_prof_error_table_r(struct et_list **list);
void initialize_prof_error_table(void);

void initialize_prof_error_table(void) {
    initialize_prof_error_table_r(&_et_list);
}

/* For Heimdal compatibility */
void initialize_prof_error_table_r(struct et_list **list)
{
    struct et_list *et, **end;

    for (end = list, et = *list; et; end = &et->next, et = et->next)
        if (et->table->msgs == text)
            return;
    et = malloc(sizeof(struct et_list));
    if (et == 0) {
        if (!link.table)
            et = &link;
        else
            return;
    }
    et->table = &et_prof_error_table;
    et->next = 0;
    *end = et;
}
