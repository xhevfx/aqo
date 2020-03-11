/*
 * assumptions.c
 *
 */


#include "postgres.h"

#include "optimizer/optimizer.h"
#include "utils/hsearch.h"

#include "aqo.h"
#include "assumptions.h"


bool use_assumptions = false;

static HTAB *htab = NULL;
static HASHCTL info;

static AssumptionVal *
see_my_prediction(List *list, double rows)
{
	ListCell *lc;
	AssumptionVal *val;

	foreach(lc, list)
	{
		val = (AssumptionVal *) lfirst(lc);

		if (val->opt_nrows == rows)
			return val;
	}

	return NULL;
}

static AssumptionVal *
add_my_prediction(List **list, double rows)
{
	AssumptionVal *val;
	MemoryContext oldCxt;

	oldCxt = MemoryContextSwitchTo(AQOMemoryContext);

	/* Not found. Add */
	val = (AssumptionVal *) palloc(sizeof(AssumptionVal));
	val->opt_nrows = clamp_row_est(rows);
	val->aqo_nrows = clamp_row_est(rows * sel_trust_factor);
	*list = lappend(*list, (void *) val);

	MemoryContextSwitchTo(oldCxt);
	return val;
}

static bool
drop_my_prediction(List **list, double rows)
{
	ListCell *lc;
	AssumptionVal *val;

	foreach(lc, *list)
	{
		val = (AssumptionVal *) lfirst(lc);

		if (val->aqo_nrows == rows)
		{
			*list = list_delete_ptr(*list, val);
			return true;
		}
	}

	return false;
}

double
do_assumption(int space, int hash, double rows)
{
	Assumption *value;
	AssumptionVal *val;

	if (!use_assumptions)
		return rows;

	Assert(aqo_mode != AQO_MODE_DISABLED);

	if (htab == NULL)
	{
		memset(&info, 0, sizeof(info));
		info.keysize = sizeof(AssumptionKey);
		info.entrysize = sizeof(Assumption);
		info.hcxt = AQOMemoryContext;
		htab = hash_create("AQO_ASSUMPTIONS", 100, &info,
										HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);
	}

	value = get_assumption(space, hash);
	if (value == NULL)
	{
		AssumptionKey key = {space, hash};

		if (aqo_mode == AQO_MODE_FROZEN)
			return rows;

		Assert(key.hash != 0);

		value = (Assumption *) hash_search(htab, (void *) &key, HASH_ENTER, NULL);
		value->inPlan = false;
		/* XXX: Check memory context */
		value->assumptions = NIL;
		val = add_my_prediction(&value->assumptions, rows);
		value->counter = 0;
	}
	else if (value->inPlan)
	{
		val = see_my_prediction(value->assumptions, rows);

		if (val != NULL)
			/* Previous assumption wasn't verified during learning stage. */
			val->aqo_nrows = clamp_row_est(val->aqo_nrows * 10.);
		else
			val = add_my_prediction(&value->assumptions, rows);

		elog(WARNING, "INPLAN. rows: %lf card: %lf (%d %d)", rows, val->aqo_nrows, space, hash);
		value->inPlan = false;
	}
	else
	{
		val = see_my_prediction(value->assumptions, rows);

		if (val == NULL)
			val = add_my_prediction(&value->assumptions, rows);
		/*
		 * The assumption wasn't used in any plan. We can use it repeatedly
		 */
		elog(WARNING, "REPEAT. rows: %lf card: %lf (%d %d)", rows, val->aqo_nrows, space, hash);
	}

	value->counter++;
	return val->aqo_nrows;
}

/*
 * Extract assumptions from hash table and store it into the tuple store.
 */
void
store_assumptions(Tuplestorestate *tupstore, TupleDesc tupdesc)
{
	HASH_SEQ_STATUS status;
	Assumption *value;
	Datum values[6];
	bool nulls[6];

	if (htab == NULL)
		return;

	hash_seq_init(&status, htab);

	while ((value = (Assumption *) hash_seq_search(&status)) != NULL)
	{
		MemSet(values, 0, sizeof(values));
		MemSet(nulls, 0, sizeof(nulls));

		values[0] = Int32GetDatum(value->key.space);
		values[1] = Int32GetDatum(value->key.hash);
		values[2] = Float8GetDatum(0);
		values[3] = Float8GetDatum(0);
		values[4] = BoolGetDatum(value->inPlan);
		values[5] = Int32GetDatum(value->counter);

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}

	/* clean up and return the tuplestore */
	tuplestore_donestoring(tupstore);
}

Assumption *
get_assumption(int space, int hash)
{
	AssumptionKey key = {space, hash};

	if (htab == NULL)
		return NULL;

	return (Assumption *) hash_search(htab, (void *) &key, HASH_FIND, NULL);
}

bool
drop_assumption(int space, int hash, double rows)
{
	AssumptionKey key = {space, hash};
	Assumption *value;
	bool found = false;

	if (htab == NULL)
		return false;

	value = (Assumption *) hash_search(htab, (void *) &key, HASH_FIND, NULL);

	if (value != NULL)
	{
		found = drop_my_prediction(&value->assumptions, rows);

		if (value->assumptions == NIL)
			(void *) hash_search(htab, (void *) &key, HASH_REMOVE, NULL);
	}

	return found;
}
