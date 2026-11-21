#!/usr/bin/env qore

%requires oracle
printf("\nQore named types test - objects\n\n");

my string $connstr;
if (!$ENV.QORE_DB_CONNSTR_ORACLE) {
    stderr.printf("ERROR: QORE_DB_CONNSTR_ORACLE is not set\n");
    exit(1);
}
$connstr = $ENV.QORE_DB_CONNSTR_ORACLE;

my Datasource $db($connstr);
$db.open();

printf("OBJECT IN\n");
my hash $obj = ("A_TEXT": "1",
                "A_NUMBER": -2.1n,
                "A_NUM_1": 1,
                "A_TEXTC" : "foo bar",
                "A_CLOB" : "lorem ipsum clob sir amet",
                "A_DATE" : now_ms(),
                "A_TSTAMP" : now_ms(),
                "A_TSTAMP_TZ" : now_ms(),
                "A_OBJECT" : bindOracleObject("TEST_OBJECT_2", ("TEXT2" : "foobar", "NUMBER2" : 5050553111079))
                );

my $r = $db.exec("begin qore_test.do_obj(%v, :retval); end;",
                 bindOracleObject("TEST_OBJECT", $obj),
                 Type::String);
printf("object: %N\n", $r);
$db.rollback();


printf("OBJECT IN\n");
my hash $obj1 = ("A_TEXT": "1",
                );

my $r2 = $db.exec("begin qore_test.do_obj(%v, :retval); end;",
                 bindOracleObject("TEST_OBJECT", $obj1),
                 Type::String);
printf("object: %N\n", $r2);
$db.rollback();



printf("\nOBJECT OUT\n");
my $r1 = $db.exec("begin qore_test.get_obj(:retval); end;", placeholderOracleObject("TEST_OBJECT"));
printf("retval: %N\n", $r1);
$db.rollback();


printf("\nOBJECT direct insert\n");
{
    on_success $db.commit();
    on_error $db.rollback();
    my $rins = $db.exec("insert into t_object_test values (%v, %v, %v)",
                        1,
                        bindOracleObject("TEST_tab_OBJECT", $obj),
                        bindOracleCollection("TEST_tab_coll", (2, 5050553111079,NULL, NOTHING, 666,))
                       );
    printf("rins: %N\n", $rins);
}

printf("\nOBJECT direct select\n");
# my $rsel = $db.select("select * from t_object_test where rownum < 5");
my $rsel = $db.select("select t.* from t_object_test t");
printf("rsel: %N\n", $rsel);

