

sub test(string $connstr, string $sql)
{
    my Datasource $ds($connstr);
    my SQLStatement $s($ds);
    $s.prepare($sql);
    on_exit $ds.rollback();

    while ($s.next())
    {
#        printf("%N\n", $s.fetchRow());
        printf("%N\n", $s.describe());
        break;
    }
}

if (!$ENV.QORE_DB_CONNSTR_ORACLE) {
    stderr.printf("ERROR: QORE_DB_CONNSTR_ORACLE is not set\n");
    exit(1);
}
test($ENV.QORE_DB_CONNSTR_ORACLE, "select rowid, p.* from step_type p");

