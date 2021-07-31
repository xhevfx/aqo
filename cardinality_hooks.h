#ifndef CARDINALITY_HOOKS_H
#define CARDINALITY_HOOKS_H

#include "optimizer/planner.h"
#include "utils/selfuncs.h"

extern estimate_num_groups_hook_type prev_estimate_num_groups_hook;

extern double aqo_estimate_num_groups_hook(PlannerInfo *root, List *groupExprs,
										   Path *subpath,
										   RelOptInfo *grouped_rel,
										   List **pgset,
										   EstimationInfo *estinfo);

extern void aqo_index_fetch_estimation_hook(IndexPath *path, List *quals,
											  double *numIndexTuples,
											  Selectivity *selectivity);

#endif /* CARDINALITY_HOOKS_H */
