CREATE TABLE public.aqo_assumptions (
	fspace_hash		int,
	fsspace_hash	int,
	counter			int NOT NULL,
	cardinality		bigint NOT NULL,
	PRIMARY KEY(fspace_hash, fsspace_hash)
);

CREATE OR REPLACE FUNCTION public.aqo_show_assumptions()
RETURNS TABLE (
	"hash"					INT,
	"fss"					INT,
	"cardinality"			DOUBLE PRECISION,
	"planner_estimation"	DOUBLE PRECISION,
	"inPlan"				BOOLEAN,
	"counter"				INT
) 
AS 'MODULE_PATHNAME','aqo_show_assumptions' LANGUAGE C;
