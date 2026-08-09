#!/bin/bash

if [ -n "$DOCKER_ORACLE" ]; then
    # default PDB for gvenzl/oracle-free image
    if [ -z "$ORACLE_PDB" ]; then
        ORACLE_PDB=FREEPDB1
    fi

    echo "LISTENER = (ADDRESS = (PROTOCOL = TCP)(HOST = oracle)(PORT = 1521))" >> /usr/lib/oracle/tnsnames.ora
    echo "oracle =
(DESCRIPTION =
  (ADDRESS_LIST =
    (ADDRESS = (PROTOCOL = TCP)(HOST = oracle)(PORT = 1521))
  )
  (CONNECT_DATA =
    (UR=A)
    (SERVICE_NAME = ${ORACLE_PDB})
  )
)" >> /usr/lib/oracle/tnsnames.ora

    # setup environment
    echo export ORACLE_SID=FREE >> /tmp/env.sh
    echo export ORACLE_PDB=${ORACLE_PDB} >> /tmp/env.sh
    echo export ORACLE_PWD=omq >> /tmp/env.sh
    echo export DOCKER_ORACLE=1 >> /tmp/env.sh
    echo export SYS_OMQ_DB_STRING=oracle:system/omq@oracle >> /tmp/env.sh
    echo export QORE_DB_CONNSTR_ORACLE=oracle:omq/omq@oracle >> /tmp/env.sh

    . /tmp/env.sh
    set +e
    waited=0
    max_tries=60
    sleep_secs=10
    echo "Waiting for Oracle DB to start."
    while true; do
        out=`qore -ne "Datasource d(\"${SYS_OMQ_DB_STRING}\"); auto x=d.getServerVersion();" 2>&1`
        status=$?
        if [ "$status" = "0" ]; then
            echo && echo "Oracle DB started after $((waited * sleep_secs)) seconds."
            echo "Creating tablespaces and omq user..."
            out=`qore -ne "
Datasource ds(\"${SYS_OMQ_DB_STRING}\");
ds.exec(\"create tablespace omq_data datafile 'omq_data01.dbf' size 128M autoextend on next 64M\");
ds.exec(\"create tablespace omq_index datafile 'omq_index01.dbf' size 64M autoextend on next 32M\");
ds.exec(\"create user omq identified by omq default tablespace omq_data temporary tablespace temp\");
ds.exec(\"grant create session, create procedure, create sequence, create table, create trigger, create type, create view, unlimited tablespace to omq\");
ds.commit();
" 2>&1`
            if [ $? != 0 ]; then
                # not necessarily fatal; the objects may already exist when run against an existing DB
                echo "warning: error creating Oracle tablespaces and omq user:"
                echo "$out"
            else
                echo "Oracle tablespaces and omq user created."
            fi
            # the test user connection is the actual precondition for the test suite, so verify it here;
            # otherwise every test reports an unrelated connection error instead of the real cause
            out=`qore -ne "Datasource d(\"${QORE_DB_CONNSTR_ORACLE}\"); auto x=d.getServerVersion();" 2>&1`
            if [ $? != 0 ]; then
                echo "ERROR: cannot connect to the Oracle DB with ${QORE_DB_CONNSTR_ORACLE}:"
                echo "$out"
                exit 1
            fi
            echo "Oracle DB ready; verified the test connection with ${QORE_DB_CONNSTR_ORACLE}"
            break
        elif [ $waited -eq $max_tries ]; then
            echo && echo "ERROR: the Oracle DB did not start within $((max_tries * sleep_secs)) seconds; aborting the build."
            echo "last error from the DB connection attempt:"
            echo "$out"
            exit 1
        elif ! echo "$out" | grep -E "QoreOracleConnection::logon.*ORA-(01033|12514|12541)" > /dev/null 2>&1; then
            echo "Error in Oracle initialization:"
            echo "$out"
            exit 1
        fi

        printf "."
        sleep $sleep_secs
        waited=$((waited+1))
    done
else
    echo "rippy =
(DESCRIPTION =
  (ADDRESS_LIST =
    (ADDRESS = (PROTOCOL = TCP)(HOST = rippy)(PORT = 1521))
  )
  (CONNECT_DATA =
    (SERVICE_NAME = rippy)
  )
)" >> /usr/lib/oracle/tnsnames.ora

    . /tmp/env.sh

    # setup new env vars
    export ORACLE_USER=omq_docker_test_`qore -lUtil -ne 'printf("%s", get_random_string());'`
    echo unset ORACLE_PDB >> /tmp/env.sh
    echo export ORACLE_SID=rippy >> /tmp/env.sh
    echo export ORACLE_PWD=omq >> /tmp/env.sh
    echo export ORACLE_USER=${ORACLE_USER} >> /tmp/env.sh
    echo export QORE_DB_CONNSTR_ORACLE=oracle:${ORACLE_USER}/omq@rippy%rippy:1521 >> /tmp/env.sh

    # create user for test
    qore -ne "Datasource ds(\"oracle:system/qore@rippy%rippy:1521\"); ds.exec(\"create user ${ORACLE_USER} identified by omq default tablespace omq_data temporary tablespace temp\"); ds.exec(\"grant create session, create procedure, create sequence, create table, create trigger, create type, create view, unlimited tablespace to ${ORACLE_USER}\"); ds.commit();"
    echo created oracle user ${ORACLE_USER}
fi
set -e
