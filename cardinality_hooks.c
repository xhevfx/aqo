/*
 *******************************************************************************
 *
 *	CARDINALITY ESTIMATION HOOKS
 *
 * This functions controls cardinality prediction in query optimization.
 * If use_aqo flag is false, then hooks just call default postgresql
 * cardinality estimator. Otherwise, they try to use AQO cardinality
 * prediction engine.
 * If use_aqo flag in true, hooks generate set of all clauses and all
 * absolute relids used in the relation being built and pass this
 * information to predict_for_relation function. Also these hooks compute
 * and pass to predict_for_relation marginal cardinalities for clauses.
 * If predict_for_relation returns non-negative value, then hooks assume it
 * to be true cardinality for given relation. Negative returned value means
 * refusal to predict cardinality. In this case hooks also use default
 * postgreSQL cardinality estimator.
 *
 *******************************************************************************
 *
 * Copyright (c) 2016-2021, Postgres Professional
 *
 * IDENTIFICATION
 *	  aqo/cardinality_hooks.c
 *
 */

#include "aqo.h"

static void call_default_set_baserel_rows_estimate(PlannerInfo *root,
									   RelOptInfo *rel);
static double call_default_get_parameterized_baserel_size(PlannerInfo *root,
											RelOptInfo *rel,
											List *param_clauses);
static void call_default_set_joinrel_size_estimates(PlannerInfo *root,
										RelOptInfo *rel,
										RelOptInfo *outer_rel,
										RelOptInfo *inner_rel,
										SpecialJoinInfo *sjinfo,
										List *restrictlist);
static double call_default_get_parameterized_joinrel_size(PlannerInfo *root,
											RelOptInfo *rel,
											Path *outer_path,
											Path *inner_path,
											SpecialJoinInfo *sjinfo,
											List *restrict_clauses);


/*
 * Calls standard set_baserel_rows_estimate or its previous hook.
 */
void
call_default_set_baserel_rows_estimate(PlannerInfo *root, RelOptInfo *rel)
{
	if (prev_set_baserel_rows_estimate_hook)
		prev_set_baserel_rows_estimate_hook(root, rel);
	else
		set_baserel_rows_estimate_standard(root, rel);
}

/*
 * Calls standard get_parameterized_baserel_size or its previous hook.
 */
double
call_default_get_parameterized_baserel_size(PlannerInfo *root,
											RelOptInfo *rel,
											List *param_clauses)
{
	if (prev_get_parameterized_baserel_size_hook)
		return prev_get_parameterized_baserel_size_hook(root, rel, param_clauses);
	else
		return get_parameterized_baserel_size_standard(root, rel, param_clauses);
}

/*
 * Calls standard get_parameterized_joinrel_size or its previous hook.
 */
double
call_default_get_parameterized_joinrel_size(PlannerInfo *root,
											RelOptInfo *rel,
											Path *outer_path,
											Path *inner_path,
											SpecialJoinInfo *sjinfo,
											List *restrict_clauses)
{
	if (prev_get_parameterized_joinrel_size_hook)
		return prev_get_parameterized_joinrel_size_hook(root, rel,
														outer_path,
														inner_path,
														sjinfo,
														restrict_clauses);
	else
		return get_parameterized_joinrel_size_standard(root, rel,
													   outer_path,
													   inner_path,
													   sjinfo,
													   restrict_clauses);
}

/*
 * Calls standard set_joinrel_size_estimates or its previous hook.
 */
void
call_default_set_joinrel_size_estimates(PlannerInfo *root, RelOptInfo *rel,
										RelOptInfo *outer_rel,
										RelOptInfo *inner_rel,
										SpecialJoinInfo *sjinfo,
										List *restrictlist)
{
	if (prev_set_joinrel_size_estimates_hook)
		prev_set_joinrel_size_estimates_hook(root, rel,
											 outer_rel,
											 inner_rel,
											 sjinfo,
											 restrictlist);
	else
		set_joinrel_size_estimates_standard(root, rel,
											outer_rel,
											inner_rel,
											sjinfo,
											restrictlist);
}

/*
 * Our hook for setting baserel rows estimate.
 * Extracts clauses, their selectivities and list of relation relids and
 * passes them to predict_for_relation.
 */
void
aqo_set_baserel_rows_estimate(PlannerInfo *root, RelOptInfo *rel)
{
	double	predicted;
	Oid		relid;
	List	*relids = NIL;
	List	*selectivities = NIL;
	List	*clauses = NIL;
	int		fss = 0;

	if (query_context.use_aqo || query_context.learn_aqo)
	{
		clauses = list_copy(rel->baserestrictinfo);
		selectivities = get_selectivities(root, clauses, 0, JOIN_INNER, NULL);

		relid = planner_rt_fetch(rel->relid, root)->relid;
		relids = list_make1_int(relid);

		/* Save prediction signature into the RelOptInfo */
		rel->aqo_specific.relids = relids;
		rel->aqo_specific.selectivities = selectivities;
		rel->aqo_specific.clauses = clauses;

		predicted = predict_for_relation(clauses, selectivities, relids, &fss);
		rel->fss_hash = fss;
	}

	if (!query_context.use_aqo)
	{
		call_default_set_baserel_rows_estimate(root, rel);
		return;
	}

	if (predicted >= 0)
	{
		rel->rows = predicted;
		rel->predicted_cardinality = predicted;
	}
	else
	{
		call_default_set_baserel_rows_estimate(root, rel);
		rel->predicted_cardinality = -1.;
	}
}

/*
 * Our hook for estimating parameterized baserel rows estimate.
 * Extracts clauses (including parametrization ones), their selectivities
 * and list of relation relids and passes them to predict_for_relation.
 */
double
aqo_get_parameterized_baserel_size(PlannerInfo *root,
								   RelOptInfo *rel,
								   List *param_clauses)
{
	double		predicted;
	Oid			relid = InvalidOid;
	List		*relids = NIL;
	List		*clauses = NIL;
	List		*selectivities = NIL;
	ListCell	*l;
	ListCell	*l2;
	int			nargs;
	int			*args_hash;
	int			*eclass_hash;
	int			current_hash;
	int			fss = 0;

	if (query_context.use_aqo || query_context.learn_aqo)
	{
		clauses = list_concat(list_copy(param_clauses),
								 list_copy(rel->baserestrictinfo));
		selectivities = get_selectivities(root, clauses, rel->relid,
										  JOIN_INNER, NULL);
		relid = planner_rt_fetch(rel->relid, root)->relid;
		get_eclasses(clauses, &nargs, &args_hash, &eclass_hash);
		forboth(l, clauses, l2, selectivities)
		{
			current_hash = get_clause_hash(
										((RestrictInfo *) lfirst(l))->clause,
										   nargs, args_hash, eclass_hash);
			cache_selectivity(current_hash, rel->relid, relid,
							  *((double *) lfirst(l2)));
		}
		pfree(args_hash);
		pfree(eclass_hash);

		relids = list_make1_int(relid);

		/* Save prediction signature into the RelOptInfo */
		rel->aqo_specific.relids = relids;
		rel->aqo_specific.ppi.pclauses = list_copy(param_clauses);
		predicted = predict_for_relation(clauses, selectivities, relids, &fss);
	}

	if (!query_context.use_aqo)
		return call_default_get_parameterized_baserel_size(root, rel,
														   param_clauses);
	rel->aqo_specific.ppi.predicted = predicted;
	rel->aqo_specific.ppi.fss = fss;

	if (predicted >= 0)
		return predicted;
	else
		return call_default_get_parameterized_baserel_size(root, rel,
														   param_clauses);
}

/*
 * Our hook for setting joinrel rows estimate.
 * Extracts clauses, their selectivities and list of relation relids and
 * passes them to predict_for_relation.
 */
void
aqo_set_joinrel_size_estimates(PlannerInfo *root, RelOptInfo *rel,
							   RelOptInfo *outer_rel,
							   RelOptInfo *inner_rel,
							   SpecialJoinInfo *sjinfo,
							   List *restrictlist)
{
	double	predicted;
	List	*relids = NIL;
	List	*outer_clauses;
	List	*inner_clauses;
	List	*allclauses;
	List	*selectivities;
	List	*inner_selectivities;
	List	*outer_selectivities;
	List	*current_selectivities = NIL;
	int		fss = 0;

	if (query_context.use_aqo || query_context.learn_aqo)
	{
		current_selectivities = get_selectivities(root, restrictlist, 0,
												  sjinfo->jointype, sjinfo);
		relids = get_list_of_relids(root, rel->relids);

		/* Save prediction signature into the RelOptInfo */
		rel->aqo_specific.relids = relids;
		rel->aqo_specific.selectivities = current_selectivities;
		rel->aqo_specific.clauses = list_copy(restrictlist);
	}

	outer_clauses = get_path_clauses(outer_rel->cheapest_total_path, root,
									 &outer_selectivities);
	inner_clauses = get_path_clauses(inner_rel->cheapest_total_path, root,
									 &inner_selectivities);
	allclauses = list_concat(list_copy(restrictlist),
							 list_concat(outer_clauses, inner_clauses));
	selectivities = list_concat(list_copy(current_selectivities),
								list_concat(outer_selectivities,
											inner_selectivities));

	predicted = predict_for_relation(allclauses, selectivities, relids, &fss);
	rel->fss_hash = fss;

	if (!query_context.use_aqo)
	{
		call_default_set_joinrel_size_estimates(root, rel,
												outer_rel,
												inner_rel,
												sjinfo,
												restrictlist);
		return;
	}

	if (predicted >= 0)
	{
		rel->predicted_cardinality = predicted;
		rel->rows = predicted;
	}
	else
	{
		rel->predicted_cardinality = -1;
		call_default_set_joinrel_size_estimates(root, rel,
												outer_rel,
												inner_rel,
												sjinfo,
												restrictlist);
	}

	list_free(selectivities);
	list_free(allclauses);
}

/*
 * Our hook for estimating parameterized joinrel rows estimate.
 * Extracts clauses (including parametrization ones), their selectivities
 * and list of relation relids and passes them to predict_for_relation.
 */
double
aqo_get_parameterized_joinrel_size(PlannerInfo *root,
								   RelOptInfo *rel,
								   Path *outer_path,
								   Path *inner_path,
								   SpecialJoinInfo *sjinfo,
								   List *restrict_clauses)
{
	double		predicted;
	List	   *relids = NIL;
	List	   *outer_clauses;
	List	   *inner_clauses;
	List	   *allclauses;
	List	   *selectivities;
	List	   *inner_selectivities;
	List	   *outer_selectivities;
	List	   *current_selectivities = NIL;
	int			fss = 0;

	if (query_context.use_aqo || query_context.learn_aqo)
	{
		current_selectivities = get_selectivities(root, restrict_clauses, 0,
												  sjinfo->jointype, sjinfo);

		relids = get_list_of_relids(root, rel->relids);

		/*
		 * Save prediction signature into the RelOptInfo.
		 * We do it for both, learn and using cases.
		 */
		rel->aqo_specific.relids = relids;
		rel->aqo_specific.selectivities = current_selectivities;
		rel->aqo_specific.clauses = list_copy(restrict_clauses);
	}

	if (!query_context.use_aqo)
		return call_default_get_parameterized_joinrel_size(root, rel,
														   outer_path,
														   inner_path,
														   sjinfo,
														   restrict_clauses);

	outer_clauses = get_path_clauses(outer_path, root, &outer_selectivities);
	inner_clauses = get_path_clauses(inner_path, root, &inner_selectivities);
	allclauses = list_concat(list_copy(restrict_clauses),
							 list_concat(outer_clauses, inner_clauses));
	selectivities = list_concat(list_copy(current_selectivities),
								list_concat(outer_selectivities,
											inner_selectivities));

	predicted = predict_for_relation(allclauses, selectivities, relids, &fss);

	rel->aqo_specific.ppi.predicted = predicted;
	rel->aqo_specific.ppi.fss = fss;

	if (predicted >= 0)
		return predicted;
	else
		return call_default_get_parameterized_joinrel_size(root, rel,
														   outer_path,
														   inner_path,
														   sjinfo,
														   restrict_clauses);

	list_free(selectivities);
	list_free(allclauses);
}
