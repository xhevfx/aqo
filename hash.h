/*
 * hash.h
 *
 */

#ifndef CONTRIB_AQO_HASH_H_
#define CONTRIB_AQO_HASH_H_

#include "postgres.h"

#include "nodes/nodes.h"

typedef int hash_t;

extern int get_query_hash(Query *parse, const char *query_text);
extern int get_fss_for_object(List *clauselist, List *selectivities,
						List *relidslist, int *nfeatures, double **features);
extern hash_t *get_eclasses(List *clauselist, int *nargs, hash_t **args_hash);
extern hash_t get_clause_hash(Expr *clause, int nargs, hash_t *args_hash,
							  hash_t *eclass_hash);


#endif /* CONTRIB_AQO_HASH_H_ */
