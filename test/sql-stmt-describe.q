

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

test("oracle:omq/omq@xbox", "select rowid, p.* from step_type p");
#test("pgsql:omq/omq@omq%localhost:5432", "select * from step_type");
#test("pgsql:omq/omq@omq%localhost:5432", "select count(1) from step_type");
#test("mysql:omq/omq@omq", "select * from step_type");

