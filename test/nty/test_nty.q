#!/usr/bin/env qore

%requires oracle

const TYPE_NUMBER = 0;
const TYPE_STRING = 1;
const TYPE_OBJECT = 2;

const NUM_1_999 = 898;
const NUM_INT = 456;
const NUM_DBL = 3.1415927;

const STR_1 = "c";
const STR_10 = "nzsuxobymr";
const STR_100 = "ziafgnjvmiijgezxivssbooduhfkzdschhwnpopcimbxrnxaqqhcnavuqfpfrbyuqfwvspaqiuptblelxmnmeoxxnmthqnwzrysk";
const STR_1000 = "hazmsuxjfhiqefmoxnumctthzjqzksnmegkweyrmryjhkpreejwvkchhjpmcnkvkosmiddapleunjlletmvuuutowwzrjmxfqwqotstqoqjunslcbhxifpzkpmanqyvcblcdeeuhdeoimeryexzocwqgjrckrospclxsxfkfmmqstqiujabsulikceovtmxfqmajkcdrjqgvnzasrddlabaeqlnqsikcyrcdtmlsqioiljduirguoykgxo";
const STR_DATE = "11-SEP-14";
const STR_FDATE = "2014-09-11 00:00:00.000000 Thu +02:00 (CEST)";
const STR_DATE_PLUS_1 = "2014-09-12 00:00:00.000000 Fri +02:00 (CEST)";
const STR_DATE_MINUS_100 = "2014-06-03 00:00:00.000000 Tue +02:00 (CEST)";
const TIMESTAMP_STR = "11-SEP-14 12.00.00.000000 AM +02:00";

our date $c_date =  date("2014-09-11");

const STR_FOO = "foo";
const STR_BAR = "bar";
const STR_END = "end";
const STR_CLOB = "lorem ipsum clob sir amet";
const STR_NULL = "{NULL}";

const OBJ_SUPP = (STR_FOO, NUM_1_999);

const OBJ_HASH_MAIN = ( "A_TEXT"  : STR_1,
                        "A_NUMBER": NUM_INT,
                        "A_TEXTC" : STR_FOO,
                        "A_CLOB"  : STR_CLOB,
                        "A_DATE"  : STR_DATE,
                        );
const OBJ_HASH_MAIN_IN = (STR_1, NUM_INT, STR_FOO, STR_CLOB, "01-JAN-70");

const OBJ_HASH_SUPP = ("TEXT2" : STR_FOO, "NUMBER2" : NUM_1_999);

const OBJ_HASH_IN = ( "A_TEXT"   : STR_1,
                      "A_NUMBER" : NUM_INT,
                      "A_TEXTC"  : STR_FOO,
                      "A_CLOB"   : STR_CLOB,
                      "A_DATE"   : STR_FDATE,
                      "A_OBJECT" : ("TEXT2"  : STR_10,
                                    "NUMBER2": NUM_1_999,),
                      );

const VARCHAR2_LST_IN = (STR_FOO, STR_BAR, NULL, NOTHING, STR_END);
const VARCHAR2_LST_OUT = (STR_10, STR_100, NULL, STR_1);
const NUM_LST_IN = (NUM_INT,  NUM_INT, NULL, NOTHING, NULL, "456", 456n, NUM_INT);
const NUM_LST_OUT = (NUM_INT, NUM_INT, NULL, NUM_INT);
const CLOB_LST_IN = (STR_FOO, STR_BAR, NULL, NOTHING, STR_END);
const CLOB_LST_OUT = (STR_100, STR_100, NULL, STR_100);
const DATE_LST_IN = (STR_DATE, STR_DATE, NULL, NOTHING, STR_DATE);
const DATE_LST_OUT = (STR_DATE_MINUS_100, STR_FDATE, NULL, STR_DATE_PLUS_1);
const OBJ_LST_OUT = (OBJ_HASH_IN, NULL, OBJ_HASH_IN, OBJ_HASH_IN);
const STR_LST_IN = ((STR_FOO, STR_BAR), (STR_FOO, STR_BAR), (STR_FOO, STR_BAR));
const STR_LST_OUT =(VARCHAR2_LST_OUT, NULL, VARCHAR2_LST_OUT, VARCHAR2_LST_OUT);
const TIMESTAMP_LST_IN = (TIMESTAMP_STR, TIMESTAMP_STR, NULL, NOTHING, TIMESTAMP_STR);

const COL_WIDTH = 100;
const OFFSET = 5;

const COLL_VARCHAR2_OUT  = 0;
const COLL_VARCHAR2_IN   = 1;
const COLL_NUMBER_IN     = 2;
const COLL_NUMBER_OUT    = 3;
const COLL_CLOB_IN       = 4;
const COLL_CLOB_OUT      = 5;
const COLL_DATE_IN       = 6;
const COLL_DATE_OUT      = 7;
const COLL_OBJECT_IN     = 8;
const COLL_OBJECT_OUT    = 9;
const COLL_STRING_IN     = 10;
const COLL_STRING_OUT    = 11;
const COLL_OBJECTS_IN    = 12;
const COLL_OBJECTS_OUT   = 13;
const COLL_TIMESTAMP_IN  = 14;
const COLL_TIMESTAMP_OUT = 15;

our int $success_count = 0;
our string $err_msg;
our string $connstr;

start();

sub start() {

	parse_command_line();

    our Datasource $db($connstr);
    $db.open();

    my int $id = 0;
    my int $prev_id = 0;
    my int $prev_success = 0;

    test_varchar2_in($id++);
    test_varchar2_out($id++);
    test_number_in($id++);
    test_number_out($id++);
    test_clob_in($id++);
    test_clob_out($id++);
	test_date_in($id++);
    test_date_out($id++);
    test_object_collection_in($id++);
    test_object_collection_out($id++);
    test_string_in($id++);
    test_string_out($id++);
    test_objects_in($id++);
    test_objects_out($id++);
    test_timestamp_in($id++);
    test_timestamp_out($id++);
    my list $row_data = ("Qore named types test - collections", sprintf("(%d/%d)", $success_count, $id));
    print_row($row_data);
    $prev_id = $id;
    $prev_success = $success_count;
    test_exception1($id++);
    test_exception2($id++);
    test_exception3($id++);
    test_exception4($id++);
    $row_data = ("Qore named types test - various errors checking", sprintf("(%d/%d)", $success_count-$prev_success, $id-$prev_id));
    print_row($row_data);
    $prev_id = $id;
    $prev_success = $success_count;

    test_object_insert($id++);
    $row_data = ("Qore named types test - objects", sprintf("(%d/%d)", $success_count-$prev_success, $id-$prev_id));
    print_row($row_data);

    $row_data = ("TESTING COMPLETED", sprintf("(%d/%d)", $success_count, $id));

    print_row($row_data);
    print_char("-", COL_WIDTH+15);
    printf("\n");

}

const opts = (
	"help"		 : "help,h",
	"datasource" : "datasource,d=i",
	);

sub usage() {
    printf(
            "usage: %s [options]	<iterations>
            -h, --help				shows this help text
            -ds,--datasource=ARG	datasource string
            \n",
            get_script_name());
    exit(1);
}


sub parse_command_line() {

	my GetOpt $g(opts);
    $o = $g.parse(\$ARGV);

    if (exists $o."_ERRORS_") {
        printf("%s\n", $o."_ERRORS_"[0]);
        exit(1);
    }
    if ($o.help)
        usage();

    $o.iters = shift $ARGV;
    if (elements $ARGV) {
        printf("error, excess arguments on command-line\n");
        usage();
    }
	if(!exists $o.iters)
	{
		if (!$ENV.QORE_DB_CONNSTR_ORACLE) {
		    stderr.printf("ERROR: QORE_DB_CONNSTR_ORACLE is not set\n");
		    exit(1);
		}
		$connstr = $ENV.QORE_DB_CONNSTR_ORACLE;
	} else {
		$connstr = $o.iters;
	}

}




sub test_varchar2_in(int id) {
    my hash $c = $db.exec("begin qore_test.do_coll(%v, :retval); end;", bindOracleCollection("COL_TEST", VARCHAR2_LST_IN));
    print_result($id, "collection IN varchar2", cmp_result(NOTHING, $c.retval, COLL_VARCHAR2_IN), VARCHAR2_LST_IN, $c.retval);
    $db.rollback();
}

sub test_varchar2_out(int id) {
    my $r2 = $db.exec("begin qore_test.get_coll(:retval); end;", placeholderOracleCollection("COL_TEST"));
    print_result($id, "collection OUT varchar2", cmp_result(VARCHAR2_LST_OUT, $r2.retval, COLL_VARCHAR2_OUT), VARCHAR2_LST_OUT, $r2);
    $db.rollback();
}

sub test_number_in(int id) {
    my $c = $db.exec("begin qore_test.do_coll_num(%v, :retval); end;", bindOracleCollection("COL_TEST_NUM", NUM_LST_IN));
    print_result($id, "collection IN number", cmp_result(NOTHING, $c.retval, COLL_NUMBER_IN), NUM_LST_IN, $c.retval);
    $db.rollback();
}

sub test_number_out(int id) {
    my $r2 = $db.exec("begin qore_test.get_coll_num(:retval); end;", placeholderOracleCollection("COL_TEST_NUM"));
    print_result($id, "collection OUT number", cmp_result(NUM_LST_OUT, $r2.retval, COLL_NUMBER_OUT), NUM_LST_OUT, $r2);
    $db.rollback();
}

sub test_clob_in(int id) {
    my $c = $db.exec("begin qore_test.do_coll_clob(%v, :retval); end;", bindOracleCollection("COL_TEST_CLOB", CLOB_LST_IN));
    print_result($id, "collection IN clob", cmp_result(NOTHING, $c.retval, COLL_CLOB_IN), CLOB_LST_IN, $c.retval);
    $db.rollback();
}

sub test_clob_out(int id) {
    my $r2 = $db.exec("begin qore_test.get_coll_clob(:retval); end;", placeholderOracleCollection("COL_TEST_clob"));
    print_result($id, "collection OUT clob", cmp_result(CLOB_LST_OUT, $r2.retval, COLL_CLOB_OUT), CLOB_LST_OUT, $r2);
    $db.rollback();
}

sub test_date_in(int id) {

	my list $inp = ($c_date, $c_date, NULL, NOTHING, $c_date);
    my $c = $db.exec("begin qore_test.do_coll_date(%v, :retval); end;", bindOracleCollection("COL_TEST_date", $inp));
    print_result($id, "collection IN date", cmp_result(NOTHING, $c.retval, COLL_DATE_IN), DATE_LST_IN, $c.retval);
    $db.rollback();
}

sub test_date_out(int id) {
    my $r3 = $db.exec("begin qore_test.get_coll_date(:retval); end;", placeholderOracleCollection("COL_TEST_date"));
	my list $cmp_list = ($c_date-100D, $c_date, NULL, $c_date+1D);
    print_result($id, "collection OUT date", cmp_result($cmp_list, $r3.retval, COLL_DATE_OUT), $cmp_list, $r3.retval);
    $db.rollback();
}

sub test_object_collection_in(int id) {
    my hash $obj = OBJ_HASH_MAIN + ( "A_OBJECT" : bindOracleObject("TEST_OBJECT_2", OBJ_HASH_SUPP),);
    my list $colo = (bindOracleObject("TEST_OBJECT", $obj), bindOracleObject("TEST_OBJECT", $obj), bindOracleObject("TEST_OBJECT", $obj));
    my $d = $db.exec("begin qore_test.do_coll_obj(%v, :retval); end;", bindOracleCollection("COL_TEST_obj", $colo));
    print_result($id, "collection IN object", cmp_result(NOTHING, $d.retval, COLL_OBJECT_IN), $colo, $d.retval);
    $db.rollback();
}

sub test_object_collection_out(int id) {
    my $r3 = $db.exec("begin qore_test.get_coll_obj(:retval); end;", placeholderOracleCollection("COL_TEST_obj"));
    print_result($id, "collection OUT object", cmp_result(OBJ_LST_OUT, $r3, COLL_OBJECT_OUT), OBJ_LST_OUT, $r3);
    $db.rollback();
}

sub test_string_in(int id) {
    my list $col = ( bindOracleCollection("COL_TEST", (STR_FOO, STR_BAR)), bindOracleCollection("COL_TEST", (STR_FOO , STR_BAR)), bindOracleCollection("COL_TEST", (STR_FOO , STR_BAR)));
    my $c = $db.exec("begin qore_test.do_coll_coll_str(%v, :retval); end;", bindOracleCollection("COL_TEST_coll_str", $col));
    print_result($id, "collection IN string", cmp_result(NOTHING, $c.retval, COLL_STRING_IN), STR_LST_IN, $c.retval);
    $db.rollback();
}

sub test_string_out(int id) {
    my $r4 = $db.exec("begin qore_test.get_coll_coll_str(:retval); end;", placeholderOracleCollection("COL_TEST_coll_str"));
    print_result($id, "collection OUT string", cmp_result(STR_LST_OUT, $r4, COLL_STRING_OUT), STR_LST_OUT, $r4);
    $db.rollback();
}

sub test_objects_in(int id) {
    my list $colco = ( bindOracleCollection("col_test_obj", (bindOracleObject("TEST_OBJECT", OBJ_HASH_MAIN), bindOracleObject("TEST_OBJECT", OBJ_HASH_MAIN))), NULL,
            bindOracleCollection("col_test_obj", (bindOracleObject("TEST_OBJECT", OBJ_HASH_MAIN), NULL, bindOracleObject("TEST_OBJECT", OBJ_HASH_MAIN)))
            );
    my $dco = $db.exec("begin qore_test.do_coll_coll_obj(%v, :retval); end;", bindOracleCollection("COL_TEST_coll_obj", $colco));
    print_result($id, "collection IN objects", cmp_result(NOTHING, $dco.retval, COLL_OBJECTS_IN), $colco, $dco.retval);
    $db.rollback();

}

sub test_objects_out(int id) {
    my $r5 = $db.exec("begin qore_test.get_coll_coll_obj(:retval); end;", placeholderOracleCollection("COL_TEST_coll_obj"));
	my list $cmp_list = (OBJ_LST_OUT, NULL, OBJ_LST_OUT, OBJ_LST_OUT);
	print_result($id, "collection IN objects", cmp_result($cmp_list, $r5.retval, COLL_OBJECTS_IN), $cmp_list, $r5.retval);
    $db.rollback();

}

sub test_timestamp_in(int id) {
    my list $colt = ($c_date, $c_date, NULL, NOTHING, $c_date);
    my $d = $db.exec("begin qore_test.do_coll_timestamp_tz(%v, :retval); end;", bindOracleCollection("COL_TEST_timestamp_tz", $colt));
    #printf("collection: %N\n", $d);
    print_result($id, "collection IN timestamp", cmp_result(NOTHING, $d.retval, COLL_TIMESTAMP_IN), TIMESTAMP_LST_IN, $d.retval);
    $db.rollback();
}

sub test_timestamp_out(int id) {
    my $r4 = $db.exec("begin qore_test.get_coll_timestamp_tz(:retval); end;", placeholderOracleCollection("COL_TEST_timestamp_tz"));
    #printf("retval: %N\n", $r4);
    my list $cmp_list = ($c_date-100D, $c_date, NULL, $c_date+1D);
    print_result($id, "collection OUT timestamp", cmp_result($cmp_list, $r4.retval, COLL_TIMESTAMP_OUT), $cmp_list, $r4.retval);
    $db.rollback();
}

sub test_exception1(int id) {
    try {
        my hash $obj =  OBJ_HASH_MAIN + ( "A_OBJECT" : bindOracleObject("TEST_OBJECT_2", OBJ_HASH_SUPP),);
        my $r = $db.exec("begin qore_test.do_obj(%v, :retval); end;",
                $obj, # missing bindOracleObject
                Type::String);
        $db.rollback();
        print_result($id, "object is not bound correctly", Qore::False, "missing bind oracle object for the api db.exec()", $r);
    }
    catch ($ex) {
        print_result($id, "object is not bound correctly", Qore::True, "missing bind oracle object for the api db.exec()", $ex);
    }
}

sub test_exception2(int id) {
    try {
        my hash $obj = OBJ_HASH_MAIN + ( "A_OBJECT" : OBJ_HASH_SUPP,);
        my $r = $db.exec("begin qore_test.do_obj(%v, :retval); end;",
                bindOracleObject("TEST_OBJECT", $obj), # missing bindOracleObject
                Type::String);
        $db.rollback();
        print_result($id, "object(sub/internal) is not bound correctly", Qore::False, "missing bind oracle object for the api db.exec()", $r);
    }
    catch ($ex) {
        print_result($id, "object(sub/internal) is not bound correctly", Qore::True, "missing bind oracle object for the api db.exec()", $ex);
    }
}

sub test_exception3(int id) {
    try {
        my list $col = (STR_FOO, STR_BAR,NULL, NOTHING, STR_END);
        my $r = $db.exec("begin qore_test.do_coll(%v, :retval); end;",
                $col, # missing bindOracleObject
                Type::String);
        $db.rollback();
        print_result($id, "collection is not bound correctly", Qore::False, "missing bind oracle object for the api db.exec()", $r);
    }
    catch ($ex) {
        print_result($id, "collection is not bound correctly", Qore::True, "missing bind oracle object for the api db.exec()", $ex);
    }
}

sub test_exception4(int id) {
    try {
        my list $colcs = ( ( STR_FOO, STR_BAR ),  );
        my $dcs = $db.exec("begin qore_test.do_coll_coll_str(%v, :retval); end;", bindOracleCollection("COL_TEST_coll_str", $colcs));
        $db.rollback();
        print_result($id, "collection(sub/internal) is not bound correctly", Qore::False, "type 'list' is not supported in binaOracleCollection()", $r);
    }
    catch ($ex) {
        print_result($id, "collection(sub/internal) is not bound correctly", Qore::True, "type 'list' is not supported in binaOracleCollection()", $ex);
    }
}

sub test_object_insert(int id) {

	on_success $db.commit();
	on_error $db.rollback();

	try {
	    # ensure we can insert with lower-case object keys
	    my hash $h;
	    map $h.($1.key.lwr()) = $1.value, OBJ_HASH_IN.pairIterator();

	    my $rins = $db.exec("insert into t_object_test values (%v, %v, %v)",
				1,
				bindOracleObject("TEST_tab_OBJECT", $h),
				bindOracleCollection("TEST_tab_coll", (NUM_INT, NUM_INT, NULL, NOTHING, NUM_INT,))
				);
	    if($rins == 1)
		print_result($id, "object direct insert", Qore::True, "object insert to db succesfull", $rins);
	    else
		print_result($id, "object direct insert", Qore::False, "object insert to db unsuccesfull", $rins);
	} catch ($ex) {
			print_result($id, "object direct insert", Qore::False, "object insert to db unsuccesfull", $ex);
	}
	printf("rins: %N\n", $rins);

}

/*
sub test_object_select() {

    printf("\nOBJECT direct select\n");
    my $rsel = $db.select("select * from t_object_test where rownum < 5");
    $rsel = $db.select("select t.* from t_object_test t");
    printf("rsel: %N\n", $rsel);

}
*/

sub print_details(int level, string type, any data, *hash support) {
    switch ($level) {
        case 0 :
            printf("%s\n", $type);
            printf("%s\n", $support.in_str);
            break;
        case 1 :
            printf("%s\n", $type);
            printf("data : \n%N\n", $data);
            printf("more :\n");
            printf("string : \n%s\n", $support.in_str);
            break;
        case 2 :
            printf("%s\n", $type);
            printf("%y\n", $data);
        default : #do nothing
    }
}

sub print_char(string c, int width) {
    for(int $i=0; $i<$width; $i++) {
        printf("%s", $c);
    }
}

sub print_row(list row_data) {

    print_char('-', COL_WIDTH);
    printf(" -----------\n");
    printf("%s ", $row_data[0]);
    print_char(' ', COL_WIDTH + OFFSET - $row_data[0].length());
    printf(" %s\n\n", $row_data[1]);

}

sub print_result(int id, string msg, bool result, any input, any output) {

    if ($result) {
        $success_count++;
        print_char('-', COL_WIDTH);
        printf(" -----------\n");
        printf("TEST-%d: %s ", $id, $msg);
        print_char(' ', COL_WIDTH - OFFSET - $msg.length());
        printf(" PASS\n");
    } else {
        print_char('-', COL_WIDTH);
        printf(" -----------\n");
        printf("TEST-%d: %s ", $id, $msg);
        print_char(' ', COL_WIDTH - OFFSET - $msg.length());
        printf(" FAIL\n");
        printf("\n");
        printf("%s\n", $msg);
        print_details(2, "INPUT", $input);
        print_details(2, "OUTUT", $output);
    }
}

bool sub cmp_result(any $input, any $output, int type) {

    if(!exists $input) {
        my string $cmp_str = get_cmp_str($type);
        if(exists $cmp_str) {
            return $cmp_str == $output;
        }
        return $input == $output;
    }
    else if($output.type() == "list") {
        if($input.size() != $output.size()) {
            sprintf($msg, "[ERROR] Input and output size doen't match : DETAILS input size==[%d] output size==[%d]", $input.size(), $output.size());
            return Qore::False;
        }
        for(my int $i=0; $i<$input.size(); $i++){
           if($input[$i] != $output[$i]) {
                printf("%s, %s\n", $input[$i].type(), $output[$i].type());
                sprintf($msg, "[ERROR] Results doesn't match : DETAILS: input==[%s] output==[%s]\n", $input[$i], $ouput[$i]);
                return Qore::False;
           }
        }
        return Qore::True;
    }
    else if($output.type() == "hash") {
        return Qore::True;
    }
    return $input == $output;

}

sub get_cmp_str(int v_type) {

    switch ($v_type) {
        case COLL_VARCHAR2_IN   : return "|" + create_cmp_str_1(VARCHAR2_LST_IN, TYPE_STRING);
        case COLL_NUMBER_IN     : return "|" + create_cmp_str_1(NUM_LST_IN, TYPE_NUMBER);
        case COLL_CLOB_IN       : return "|" + create_cmp_str_1(CLOB_LST_IN, TYPE_NUMBER);
        case COLL_DATE_IN       : return "|" + create_cmp_str_1(DATE_LST_IN, TYPE_NUMBER);
        case COLL_OBJECT_IN     :
                                    my string $cmp_str = "|";
                                    for(my int $i=0; $i<3; $i++){
                                        $cmp_str += ($i+1) + "=" +create_cmp_str_1(OBJ_HASH_MAIN_IN, TYPE_OBJECT);
                                    }
                                    return $cmp_str;
        case COLL_STRING_IN     : return create_cmp_str_1(STR_LST_IN, TYPE_STRING);
        case COLL_OBJECTS_IN    :
                                    my string $cmp_str = "\n|";
                                    for($i=0; $i<2; $i++){
                                        $cmp_str += ($i+1) + "=" + create_cmp_str_1(OBJ_HASH_MAIN_IN, TYPE_OBJECT);
                                    }
                                    $cmp_str += "\n{NULL}\n|";
                                    for($i=0; $i<3; $i++){
                                        $cmp_str += ($i+1) + "=" + create_cmp_str_1(OBJ_HASH_MAIN_IN, TYPE_OBJECT);
                                    }
                                    return $cmp_str;
        case COLL_TIMESTAMP_IN  : return "|" + create_cmp_str_1(TIMESTAMP_LST_IN, TYPE_NUMBER);
        default : return NOTHING;
    }

}

string sub getKeyValuePair(string key, any val, int v_type) {

    if($v_type == TYPE_STRING) {
        if(!exists $val) $val = "{NULL}";
        else if($val == NULL) $val = "{NULL}";
    }

    return $key + "=" + $val + "|";

}


sub create_cmp_str_1(list col, int v_type) {

    my string $ret_str = "";

    if($v_type == TYPE_OBJECT) {
        $ret_str = $col[0];
        for(my int $j=1; $j<$col.size(); $j++) {
            my any $val = $col[$j];
            $ret_str += "-" + $val;
        }
        $ret_str += "|";
    }
    else {
        for(my int $i=0; $i<$col.size(); $i++){
            if($col[$i].type() == "list"){
                if($ret_str == "|") $ret_str = "\n";
                else $ret_str += "\n";
                for(my int $j=0; $j<$col[$i].size(); $j++){
                    $ret_str += getKeyValuePair(($j+1) + "", $col[$i][$j], TYPE_STRING);
                }
            } else {
                $ret_str += getKeyValuePair(($i+1) + "", $col[$i], $v_type);
            }
        }
    }
    return $ret_str;
}
















