/* A single explicit observer shared by provider and checked-local cleanup.
 * No private Toka global is duplicated or inferred from a declaration. */
static int drops;
void replay_item_drop(void) { ++drops; }
int item_drop_count(void) { return drops; }
