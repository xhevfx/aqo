/*
 * assumptions.h
 *
 */

#ifndef CONTRIB_AQO_ASSUMPTIONS_H_
#define CONTRIB_AQO_ASSUMPTIONS_H_

typedef struct
{
	int space;
	int hash;
} AssumptionKey;

typedef struct
{
	AssumptionKey key;
	double cardinality;
	double planner_estimation;
	int counter;
	bool inPlan;
} Assumption;

extern double do_assumption(int space, int hash, double rows);
extern void store_assumptions(Tuplestorestate *tupstore, TupleDesc tupdesc);
extern Assumption *get_assumption(int space, int hash);
extern bool drop_assumption(int space, int hash);

#endif /* CONTRIB_AQO_ASSUMPTIONS_H_ */
