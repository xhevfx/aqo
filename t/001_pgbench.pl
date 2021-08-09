use strict;
use warnings;
use TestLib;
use Test::More tests => 11;
use PostgresNode;

my $node = PostgresNode->new('aqotest');
$node->init;
$node->append_conf('postgresql.conf', qq{
						shared_preload_libraries = 'aqo'
						aqo.mode = 'intelligent'
						aqo.log_ignorance = 'on'
						log_statement = 'ddl'
					});

my $TRANSACTIONS = 10;
my $CLIENTS = 20;
my $THREADS = 20;

$node->start();

# Check conflicts of accessing to the ML knowledge base
# intelligent mode
$node->safe_psql('postgres', "CREATE EXTENSION aqo");
$node->safe_psql('postgres', "ALTER SYSTEM SET aqo.mode = 'intelligent'");
$node->command_ok([ 'pgbench', '-i', '-s', '1' ], 'init pgbench tables');
$node->command_ok([ 'pgbench', '-t',
					"$TRANSACTIONS", '-c', "$CLIENTS", '-j', "$THREADS" ],
					'pgbench in intelligent mode');

$node->safe_psql('postgres', "ALTER SYSTEM SET aqo.mode = 'controlled'");
$node->command_ok([ 'pgbench', '-t',
					"$TRANSACTIONS", '-c', "$CLIENTS", '-j', "$THREADS" ],
					'pgbench in controlled mode');

$node->safe_psql('postgres', "ALTER SYSTEM SET aqo.mode = 'disabled'");
$node->command_ok([ 'pgbench', '-t',
					"$TRANSACTIONS", '-c', "$CLIENTS", '-j', "$THREADS" ],
					'pgbench in disabled mode');

$node->safe_psql('postgres', "DROP EXTENSION aqo");
$node->safe_psql('postgres', "CREATE EXTENSION aqo");

$node->safe_psql('postgres', "ALTER SYSTEM SET aqo.mode = 'learn'");
$node->command_ok([ 'pgbench', '-t',
					"$TRANSACTIONS", '-c', "$CLIENTS", '-j', "$THREADS" ],
					'pgbench in learn mode');

$node->safe_psql('postgres', "ALTER SYSTEM SET aqo.mode = 'frozen'");
$node->command_ok([ 'pgbench', '-t',
					"$TRANSACTIONS", '-c', "$CLIENTS", '-j', "$THREADS" ],
					'pgbench in frozen mode');

# ##############################################################################
#
# Check procedure of ML-knowledge data cleaning.
#
# ##############################################################################

# Store OIDs of pgbench tables
my $aoid = $node->safe_psql('postgres',
							"SELECT ('pgbench_accounts'::regclass)::oid");
my $boid = $node->safe_psql('postgres',
							"SELECT ('pgbench_branches'::regclass)::oid");
my $toid = $node->safe_psql('postgres',
							"SELECT ('pgbench_tellers'::regclass)::oid");
my $hoid = $node->safe_psql('postgres',
							"SELECT ('pgbench_history'::regclass)::oid");
diag("oids: $aoid, $boid, $toid, $hoid");

# Add data into AQO to control that cleaning procedure won't delete nothing extra
$node->safe_psql('postgres', "
	CREATE TABLE detector(a int);
	INSERT INTO detector (a) VALUES (1);
	UPDATE detector SET a = a + 1;
	DELETE FROM detector;
	SELECT count(*) FROM detector;
");

# New queries won't add rows into AQO knowledge base.
$node->safe_psql('postgres', "ALTER SYSTEM SET aqo.mode = 'disabled'");
$node->restart();
my $res = $node->safe_psql('postgres', "SHOW aqo.mode");
is($res, 'disabled');

# Number of rows in aqo_data: related to pgbench test and total value.
my $pgb_fss_count = $node->safe_psql('postgres', "
	SELECT count(*) FROM aqo_data
	WHERE	$aoid = ANY(oids) OR
			$boid = ANY(oids) OR
			$toid = ANY(oids) OR
			$hoid = ANY(oids)
");
my $fss_count = $node->safe_psql('postgres', "SELECT count(*) FROM aqo_data;");

# Number of rows in aqo_queries: related to pgbench test and total value.
my $pgb_fs_count = $node->safe_psql('postgres', "
	SELECT count(*) FROM aqo_queries
	WHERE fspace_hash IN (
		SELECT fspace_hash FROM aqo_data
		WHERE
			$aoid = ANY(oids) OR
			$boid = ANY(oids) OR
			$toid = ANY(oids) OR
			$hoid = ANY(oids)
	)
");
my $fs_count = $node->safe_psql('postgres', "SELECT count(*) FROM aqo_queries;");

# Number of rows in aqo_query_texts: related to pgbench test and total value.
my $pgb_fs_samples_count = $node->safe_psql('postgres', "
	SELECT count(*) FROM aqo_query_texts
	WHERE query_hash IN (
		SELECT fspace_hash FROM aqo_data
		WHERE $aoid = ANY(oids) OR $boid = ANY(oids) OR $toid = ANY(oids) OR $hoid = ANY(oids)
	)
");
my $fs_samples_count = $node->safe_psql('postgres', "SELECT count(*) FROM aqo_query_texts;");

# Number of rows in aqo_query_stat: related to pgbench test and total value.
my $pgb_stat_count = $node->safe_psql('postgres', "
	SELECT count(*) FROM aqo_query_texts
	WHERE query_hash IN (
		SELECT fspace_hash FROM aqo_data
		WHERE $aoid = ANY(oids) OR $boid = ANY(oids) OR $toid = ANY(oids) OR $hoid = ANY(oids)
	)
");
my $stat_count = $node->safe_psql('postgres', "SELECT count(*) FROM aqo_query_stat;");

diag("pgbench-related rows: aqo_data - $pgb_fss_count/$fss_count,
	aqo_queries: $pgb_fs_count/$fs_count, aqo_query_texts: $pgb_fs_samples_count/$fs_samples_count,
	aqo_query_stat: $pgb_stat_count/$stat_count");

$node->safe_psql('postgres', "
	DROP TABLE	pgbench_accounts, pgbench_branches, pgbench_tellers,
				pgbench_history CASCADE;");

# Clean unneeded AQO knowledge
my $pgb1 = $node->safe_psql('postgres', "
	SELECT * FROM aqo_query_texts
	WHERE query_hash NOT IN (
		SELECT fspace_hash FROM aqo_data
		WHERE $aoid = ANY(oids) OR $boid = ANY(oids) OR $toid = ANY(oids) OR $hoid = ANY(oids)
	)
");
my $pgb2 = $node->safe_psql('postgres', "
SELECT * FROM aqo_data WHERE fspace_hash = 800491386 OR fspace_hash = 63990681 OR fspace_hash = 2111770555 OR fspace_hash = 823663426
");
diag("\n---> NOT IN: |-|-| $pgb1 |-|-|\n $pgb2 |-|-|\n");
my $texts = $node->safe_psql('postgres', "SELECT * FROM aqo_query_texts;");
$node->safe_psql('postgres', "SELECT clean_aqo_data()");
my $texts1 = $node->safe_psql('postgres', "SELECT * FROM aqo_query_texts;");
diag("\n---> texts: ||| $texts |||\n $texts1 |||\n");

# Calculate total number of rows in AQO-related tables.
my $new_fs_count = $node->safe_psql('postgres', "SELECT count(*) FROM aqo_queries;");
my $new_fss_count = $node->safe_psql('postgres', "SELECT count(*) FROM aqo_data;");
my $new_fs_samples_count = $node->safe_psql('postgres', "SELECT count(*) FROM aqo_query_texts;");
my $new_stat_count = $node->safe_psql('postgres', "SELECT count(*) FROM aqo_query_stat;");
diag("Total AQO rows after dropping pgbench-related  tables:
	aqo_queries: $new_fs_count, aqo_data: $new_fss_count,
	aqo_query_texts: $new_fs_samples_count, aqo_query_stat: $new_stat_count");

# Check total number of rows in AQO knowledge base after removing of
# pgbench-related data.
is($new_fs_count == $fs_count - $pgb_fs_count, 1, 'Total number of feature spaces');
is($new_fss_count == $fss_count - $pgb_fss_count, 1, 'Total number of feature subspaces');
is($new_fs_samples_count == $fs_samples_count - $pgb_fs_samples_count, 1, 'Total number of samples in aqo_query_texts');
is($new_stat_count == $stat_count - $pgb_stat_count, 1, 'Total number of samples in aqo_query_texts');

$node->safe_psql('postgres', "DROP EXTENSION aqo");
$node->stop();
