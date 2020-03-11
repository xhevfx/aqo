/*
 * assumptions.h
 *
 */

#ifndef CONTRIB_AQO_ASSUMPTIONS_H_
#define CONTRIB_AQO_ASSUMPTIONS_H_

#include "postgres.h"
#include "nodes/nodes.h"

typedef struct
{
	int space;
	int hash;
} AssumptionKey;

typedef struct
{
	double opt_nrows; /* Planner estimation */
	double aqo_nrows; /* AQO assumption */
} AssumptionVal;

typedef struct
{
	AssumptionKey key;
	List *assumptions;
	int counter;
	bool inPlan;
} Assumption;

extern bool use_assumptions;

extern double do_assumption(int space, int hash, double rows);
extern void store_assumptions(Tuplestorestate *tupstore, TupleDesc tupdesc);
extern Assumption *get_assumption(int space, int hash);
extern bool drop_assumption(int space, int hash, double rows);

#endif /* CONTRIB_AQO_ASSUMPTIONS_H_ */
