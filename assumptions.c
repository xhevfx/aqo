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

double
do_assumption(int space, int hash, double rows)
{
	Assumption *value;

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
		value->cardinality = clamp_row_est(rows * sel_trust_factor);
		value->counter = 0;
	}
	else if (value->inPlan)
	{
		/* Previous assumption wasn't verified during learning stage. */
		value->cardinality = clamp_row_est(value->cardinality * 10.);
		value->inPlan = false;
	}
	else
	{
		/*
		 * The assumption wasn't used in any plan. We can use it repeatedly
		 */
	}

	value->planner_estimation = rows;
	value->counter++;
	return value->cardinality;
}

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
		values[2] = Float8GetDatum(value->cardinality);
		values[3] = Float8GetDatum(value->planner_estimation);
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
drop_assumption(int space, int hash)
{
	AssumptionKey key = {space, hash};
	bool found;

	if (htab == NULL)
		return false;

	found =  (hash_search(htab, (void *) &key, HASH_FIND, NULL) != NULL);

	if (found)
		(void *) hash_search(htab, (void *) &key, HASH_REMOVE, NULL);

	return found;
}
