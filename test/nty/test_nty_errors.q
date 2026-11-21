#!/usr/bin/env qore

%requires oracle
printf("\nQore named types test - various errors checking\n\n");

my string $connstr;
if (!$ENV.QORE_DB_CONNSTR_ORACLE) {
    stderr.printf("ERROR: QORE_DB_CONNSTR_ORACLE is not set\n");
    exit(1);
}
$connstr = $ENV.QORE_DB_CONNSTR_ORACLE;

my Datasource $db($connstr);
$db.open();

try {
    printf("\nObject is not bound correctly\n");
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

    my $r = $db.exec("begin qore_test.do_obj(%v, :retval); end;",
                     $obj, # missing bindOracleObject
                     Type::String);
    printf("object: %N\n", $r);
    $db.rollback();
}
catch ($ex) {
    printf("%N\n", $ex);
}


try {
    printf("\nObject (sub/internal) is not bound correctly\n");
    my hash $obj = ("A_TEXT": "1",
                    "A_NUMBER": 2,
                    "A_NUM_1": 1,
                    "A_TEXTC" : "foo bar",
                    "A_CLOB" : "lorem ipsum clob sir amet",
                    "A_DATE" : now_ms(),
                    "A_TSTAMP" : now_ms(),
                    "A_TSTAMP_TZ" : now_ms(),
                    "A_OBJECT" : ("TEXT2" : "foobar", "NUMBER2" : 666)
                    );

    my $r = $db.exec("begin qore_test.do_obj(%v, :retval); end;",
                     bindOracleObject("TEST_OBJECT", $obj), # missing bindOracleObject
                     Type::String);
    printf("object: %N\n", $r);
    $db.rollback();
}
catch ($ex) {
    printf("%N\n", $ex);
}


try {
    printf("\nCollection is not bound correctly\n");
    my list $col = 'foo', 'bar',NULL, NOTHING, "the end";

    my $r = $db.exec("begin qore_test.do_coll(%v, :retval); end;",
                     $col, # missing bindOracleObject
                     Type::String);
    printf("result: %N\n", $r);
    $db.rollback();
}
catch ($ex) {
    printf("%N\n", $ex);
}

try {
    printf("\Collection (sub/internal) is not bound correctly\n");
    my list $colcs = ( ( "foo", "bar" ),  );
    my $dcs = $db.exec("begin qore_test.do_coll_coll_str(%v, :retval); end;",
                     bindOracleCollection("COL_TEST_coll_str", $colcs));
    printf("collection: %N\n", $dcs);
    $db.rollback();
}
catch ($ex) {
    printf("%N\n", $ex);
}

/*
printf("OBJECT IN\n");
my hash $obj = (
    "A_DATE" : now_ms(),
    "A_TSTAMP" : now_ms(),
    "A_TSTAMP_TZ" : now_ms(),
    "A_INT_YM" : now_ms(),
    "A_INT_DS" : now_ms(),
    );

my $r = $db.exec("begin qore_test.do_obj_timestamp(%v, :retval); end;",
                 bindOracleObject("TEST_OBJECT_TIMESTAMP", $obj),
                 Type::String);
printf("object: %N\n", $r);
$db.rollback();
*/

printf("\nOBJECT OUT\n");
my $r1 = $db.exec("begin qore_test.get_obj_timestamp(:retval); end;", placeholderOracleObject("TEST_OBJECT_TIMESTAMP"));
printf("retval: %N\n", $r1);
$db.rollback();
