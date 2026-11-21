#!/usr/bin/env qore

%requires oracle
printf("\nQore named types test - collections\n\n");

my string $connstr;
if (!$ENV.QORE_DB_CONNSTR_ORACLE) {
    stderr.printf("ERROR: QORE_DB_CONNSTR_ORACLE is not set\n");
    exit(1);
}
$connstr = $ENV.QORE_DB_CONNSTR_ORACLE;

my Datasource $db($connstr);
$db.open();

printf("\nCOLLECTION IN varchar2\n");
my list $col = ('foo', 'bar',NULL, NOTHING, "the end");
my hash $c = $db.exec("begin qore_test.do_coll(%v, :retval); end;",
                 bindOracleCollection("COL_TEST", $col));
printf("collection: %N\n", $c);
$db.rollback();

printf("\nCOLLECTION OUT varchar2\n");
my $r2 = $db.exec("begin qore_test.get_coll(:retval); end;", placeholderOracleCollection("COL_TEST"));
printf("retval: %N\n", $r2);
$db.rollback();


printf("\nCOLLECTION IN number\n");
$col = (2, 213,NULL, NOTHING, "23932", 500n, 3.1415927);
$c = $db.exec("begin qore_test.do_coll_num(%v, :retval); end;",
                 bindOracleCollection("COL_TEST_NUM", $col));
printf("collection: %N\n", $c);
$db.rollback();

printf("\nCOLLECTION OUT number\n");
$r2 = $db.exec("begin qore_test.get_coll_num(:retval); end;", placeholderOracleCollection("COL_TEST_NUM"));
printf("retval: %N\n", $r2);
$db.rollback();


printf("\nCOLLECTION IN clob\n");
$col = ('foo', 'bar',NULL, NOTHING, "the end");
$c = $db.exec("begin qore_test.do_coll_clob(%v, :retval); end;",
                 bindOracleCollection("COL_TEST_CLOB", $col));
printf("collection: %N\n", $c);
$db.rollback();

printf("\nCOLLECTION OUT clob\n");
$r2 = $db.exec("begin qore_test.get_coll_clob(:retval); end;", placeholderOracleCollection("COL_TEST_clob"));
printf("retval: %N\n", $r2);
$db.rollback();


printf("\nCOLLECTION IN date\n");
$cold = (now(), now_ms(), NULL, NOTHING, now()+10);
$d = $db.exec("begin qore_test.do_coll_date(%v, :retval); end;",
                 bindOracleCollection("COL_TEST_date", $cold));
printf("collection: %N\n", $d);
$db.rollback();

printf("\nCOLLECTION OUT date\n");
my $r3 = $db.exec("begin qore_test.get_coll_date(:retval); end;", placeholderOracleCollection("COL_TEST_date"));
printf("retval: %N\n", $r3);
$db.rollback();


printf("\nCOLLECTION IN object\n");
my hash $obj = ("A_TEXT": "1",
                "A_NUMBER": 2,
                "A_NUM_1": 1,
                "A_TEXTC" : "foo bar",
                "A_CLOB" : "lorem ipsum clob sir amet",
                "A_DATE" : now_ms(),
                "A_TSTAMP" : now_ms(),
                "A_TSTAMP_TZ" : now_ms(),
                "A_OBJECT" : bindOracleObject("TEST_OBJECT_2", ("TEXT2" : "foobar", "NUMBER2" : 666))
                );
my list $colo = (bindOracleObject("TEST_OBJECT", $obj),
		 bindOracleObject("TEST_OBJECT", $obj),
		 bindOracleObject("TEST_OBJECT", $obj),
		 );
$d = $db.exec("begin qore_test.do_coll_obj(%v, :retval); end;",
                 bindOracleCollection("COL_TEST_obj", $colo));
printf("collection: %N\n", $d);
$db.rollback();

printf("\nCOLLECTION OUT object\n");
$r3 = $db.exec("begin qore_test.get_coll_obj(:retval); end;", placeholderOracleCollection("COL_TEST_obj"));
printf("retval:\n%N\n", $r3);
$db.rollback();


printf("\nCOLLECTION IN string collections\n");
my list $colcs = ( bindOracleCollection("col_test", ( "foo", "bar" )),
                   NULL,
                   bindOracleCollection("col_test", ( "lorem", "ipsum" ))
                 );
my $dcs = $db.exec("begin qore_test.do_coll_coll_str(%v, :retval); end;",
                 bindOracleCollection("COL_TEST_coll_str", $colcs));
printf("collection: %N\n", $dcs);
$db.rollback();

printf("\nCOLLECTION OUT string collections\n");
my $r4 = $db.exec("begin qore_test.get_coll_coll_str(:retval); end;", placeholderOracleCollection("COL_TEST_coll_str"));
printf("retval: %N\n", $r4);
$db.rollback();


printf("\nCOLLECTION IN objects collections\n");
my list $colco = ( bindOracleCollection("col_test_obj", (bindOracleObject("TEST_OBJECT", $obj), bindOracleObject("TEST_OBJECT", $obj))),
                   NULL,
                   bindOracleCollection("col_test_obj", (bindOracleObject("TEST_OBJECT", $obj), NULL, bindOracleObject("TEST_OBJECT", $obj)))
                 );
my $dco = $db.exec("begin qore_test.do_coll_coll_obj(%v, :retval); end;",
                 bindOracleCollection("COL_TEST_coll_obj", $colco));
printf("collection: %N\n", $dco);
$db.rollback();

printf("\nCOLLECTION OUT objects collections\n");
my $r5 = $db.exec("begin qore_test.get_coll_coll_obj(:retval); end;", placeholderOracleCollection("COL_TEST_coll_obj"));
printf("retval: %N\n", $r5);
$db.rollback();


printf("\nCOLLECTION IN timestamp\n");
my list $colt = (now(), now_ms(), NULL, NOTHING, now()+10);
my $d = $db.exec("begin qore_test.do_coll_timestamp_tz(%v, :retval); end;",
                 bindOracleCollection("COL_TEST_timestamp_tz", $colt));
printf("collection: %N\n", $d);
$db.rollback();

printf("\nCOLLECTION OUT timestamp\n");
$r4 = $db.exec("begin qore_test.get_coll_timestamp_tz(:retval); end;", placeholderOracleCollection("COL_TEST_timestamp_tz"));
printf("retval: %N\n", $r4);
$db.rollback();

