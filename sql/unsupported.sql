CREATE EXTENSION aqo;
SET aqo.mode = 'learn';
SET aqo.show_details = 'on';

DROP TABLE IF EXISTS t CASCADE;
CREATE TABLE t AS SELECT (gs.* / 50) AS x FROM generate_series(1,1000) AS gs;

EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF, TIMING OFF)
	SELECT * FROM t GROUP BY (x) HAVING x > 3;

-- Do not support having clauses for now.
EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF, TIMING OFF)
	SELECT * FROM t GROUP BY (x) HAVING x > 3;

--
-- Ignore subplans in clauses.
--

CREATE TABLE t1 AS (SELECT * FROM generate_series(1,10) AS id);
CREATE TABLE t2 AS (SELECT * FROM generate_series(1,10) AS id);
ANALYZE t1,t2;

--
-- With 'never executed'
--

-- Learn phase
SELECT count(*) FROM t1 WHERE EXISTS (SELECT * FROM t2 WHERE t2.id=t1.id UNION ALL SELECT * FROM t2 WHERE t2.id=t1.id);

-- Demo
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT count(*) FROM t1 WHERE EXISTS (
	SELECT * FROM t2 WHERE t2.id=t1.id
	UNION ALL
	SELECT * FROM t2 WHERE t2.id=t1.id
);

--
-- All nodes accessed, but AQO hasn't used for cardinality prediction on
-- a build of a scan node.
--

-- Learn phase
SELECT count(*) FROM t1 WHERE EXISTS (SELECT * FROM t2 WHERE t2.id>10 UNION ALL SELECT * FROM t2 WHERE t2.id=t1.id);

-- Demo
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT count(*) FROM t1 WHERE EXISTS (
	SELECT * FROM t2 WHERE t2.id>10
	UNION ALL
	SELECT * FROM t2 WHERE t2.id=t1.id
);

-- The case of OUTER JOIN

-- Learn phase
SELECT count(*) FROM t1 LEFT JOIN t2 USING(id) WHERE EXISTS (
	SELECT * FROM t2 WHERE t2.id>10
	UNION ALL
	SELECT * FROM t2 WHERE t2.id=t1.id
);

-- Demo
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT count(*) FROM t1 LEFT JOIN t2 USING(id) WHERE EXISTS (
	SELECT * FROM t2 WHERE t2.id>10
	UNION ALL
	SELECT * FROM t2 WHERE t2.id=t1.id
);

DROP TABLE IF EXISTS t,t1,t2 CASCADE;
DROP EXTENSION aqo;
