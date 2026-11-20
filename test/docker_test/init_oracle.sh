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
    echo export ORACLE_PWD=qore >> /tmp/env.sh
    echo export DOCKER_ORACLE=1 >> /tmp/env.sh
    echo export QORE_DB_CONNSTR_ORACLE_SYS=oracle:system/qore@oracle >> /tmp/env.sh
    echo export QORE_DB_CONNSTR_ORACLE=oracle:qore/qore@oracle >> /tmp/env.sh

    . /tmp/env.sh
    set +e
    . "$(dirname "$0")/oracle_wait.sh"
    # a connection attempt to the PDB as SYSTEM: the readiness signal of the service
    probe_sys() {
        qore -ne "Datasource d(\"${QORE_DB_CONNSTR_ORACLE_SYS}\"); auto x=d.getServerVersion();"
    }
    echo "Waiting for Oracle DB to start."
    if ! oracle_wait_for_db probe_sys 60 10; then
        echo "aborting the build; last error from the DB connection attempt:"
        echo "$ORACLE_WAIT_OUT"
        exit 1
    fi
    echo "Creating the test user..."
    out=`qore -ne "
Datasource ds(\"${QORE_DB_CONNSTR_ORACLE_SYS}\");
ds.exec(\"create user qore identified by qore\");
ds.exec(\"grant create session, create procedure, create sequence, create table, create trigger, create type, create view, unlimited tablespace to qore\");
ds.exec(\"grant execute on dbms_aqadm to qore\");
ds.exec(\"grant execute on dbms_aq to qore\");
ds.commit();
" 2>&1`
    if [ $? != 0 ]; then
        # not necessarily fatal; the objects may already exist when run against an existing DB
        echo "warning: error creating the Oracle test user:"
        echo "$out"
    else
        echo "Oracle test user created."
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
    export ORACLE_USER=qore_test_`qore -lUtil -ne 'printf("%s", get_random_string());'`
    echo unset ORACLE_PDB >> /tmp/env.sh
    echo export ORACLE_SID=rippy >> /tmp/env.sh
    echo export ORACLE_PWD=qore >> /tmp/env.sh
    echo export ORACLE_USER=${ORACLE_USER} >> /tmp/env.sh
    echo export QORE_DB_CONNSTR_ORACLE=oracle:${ORACLE_USER}/qore@rippy%rippy:1521 >> /tmp/env.sh

    # create user for test
    qore -ne "Datasource ds(\"oracle:system/qore@rippy%rippy:1521\"); ds.exec(\"create user ${ORACLE_USER} identified by qore\"); ds.exec(\"grant create session, create procedure, create sequence, create table, create trigger, create type, create view, unlimited tablespace to ${ORACLE_USER}\"); ds.exec(\"grant execute on dbms_aqadm to ${ORACLE_USER}\"); ds.exec(\"grant execute on dbms_aq to ${ORACLE_USER}\"); ds.commit();"
    echo created oracle user ${ORACLE_USER}
fi
set -e
