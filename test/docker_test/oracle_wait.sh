# Copyright (C) 2026 Qore Technologies, s.r.o.
#
# sourced by init_oracle.sh (and by test-init-oracle-wait.sh): waits for the gvenzl/oracle-free service container

# oracle_error_codes <output> [<text to print if the output has no code>]
#
# Prints the exception code and the distinct ORA codes of the output of a failed command, and nothing else of it: the
# description of an exception can contain the connection string.
oracle_error_codes() {
    local exception codes
    exception=$(printf '%s\n' "$1" | sed -n -E 's/^([A-Z][A-Z0-9_]*((:[A-Z0-9_-]+)|(-[A-Z0-9_]+))+): .*/\1/p' \
        | head -n 1)
    case $exception in
        ORA-*) exception= ;;
    esac
    codes=$(printf '%s\n' "$1" | tr -c 'A-Za-z0-9_-' '\n' | grep -E '^ORA-[0-9]+$' | sort -u | tr '\n' ' ')
    codes=${codes% }
    if [ -n "$exception" ] && [ -n "$codes" ]; then
        echo "$exception $codes"
    elif [ -n "$exception$codes" ]; then
        echo "$exception$codes"
    elif [ -n "${2:-}" ]; then
        echo "$2"
    fi
}

# oracle_wait_for_db <probe> <max tries> <seconds between tries>
#
# Calls <probe> (a command or function that prints the output of a connection attempt and returns its exit status)
# until it succeeds, and returns:
# - 0: the probe succeeded
# - 1: the listener of the service answered and is gone: the service container has exited
# - 2: the probe did not succeed within <max tries> further attempts
# - 3: the probe failed with an error that the service does not report while it starts
# The last output of the probe is in ORACLE_WAIT_OUT.  Requires bash (SECONDS).
oracle_wait_for_db() {
    local probe=$1 max_tries=$2 sleep_secs=$3
    local waited=0 status err
    # the error of the previous attempt
    local last_err=
    # set once the service's listener has answered
    local listener_seen=
    # the number of consecutive attempts that found no listener after it had answered
    local listener_gone=0
    local in_ci=
    if [ -n "${GITLAB_CI:-}" ]; then
        in_ci=1
    fi
    SECONDS=0
    while true; do
        ORACLE_WAIT_OUT=`$probe 2>&1`
        status=$?
        if [ "$status" = "0" ]; then
            echo && echo "Oracle DB started after ${SECONDS} seconds."
            return 0
        fi
        err=`printf '%s\n' "$ORACLE_WAIT_OUT" | tr -c 'A-Za-z0-9_-' '\n' | grep -E '^ORA-[0-9]+$' | head -n 1`
        if [ -z "$err" ]; then
            # no Oracle error: the exception code identifies the error; this text is logged
            err=`oracle_error_codes "$ORACLE_WAIT_OUT" "(no error code in the output, exit status $status)"`
        fi
        # each change of the error is logged with the time since the wait started, so that the log shows how far the
        # service got
        if [ "$err" != "$last_err" ]; then
            echo && echo "after ${SECONDS}s: $err"
            last_err=$err
        fi
        # ORA-12541: no listener
        if [ "$err" = "ORA-12541" ]; then
            if [ -n "$listener_seen" ]; then
                listener_gone=$((listener_gone+1))
            fi
        else
            listener_seen=1
            listener_gone=0
        fi

        # the image starts its listener ("lsnrctl start" in container-entrypoint.sh) before the database and never
        # stops or restarts it until the container stops: a listener that answered and is gone means that the
        # service container has exited, and it is never started again (job pods have restartPolicy "Never"), so
        # waiting longer cannot help.  Two attempts are required, so that a single refused connection (a transient
        # network fault) is not taken for a dead container.  (Job 239973 waited out the whole deadline for a service container
        # that had exited with status 24, 48 seconds after it started.)
        if [ $listener_gone -ge 2 ]; then
            echo && echo "ERROR: the listener of the Oracle service answered and is gone (${SECONDS}s): the service container has exited."
            if [ -n "$in_ci" ]; then
                echo "The output of the service container is in this job's log (CI_DEBUG_SERVICES)."
            fi
            return 1
        fi
        # errors that the gvenzl/oracle-free service container reports while it is still starting are waited out
        # (bounded by max_tries); any other error is fatal at once:
        # - ORA-01033 initialization or shutdown in progress, ORA-01034 Oracle not available, ORA-01089 immediate
        #   shutdown in progress
        # - ORA-01109 database not open: the pluggable database is opened after the listener accepts connections
        # - ORA-01017 invalid credential: the container sets the SYSTEM password from ORACLE_PASSWORD during its first
        #   boot, so the password is refused until initialization completes
        # - ORA-12514 / ORA-12528 / ORA-12541: the listener does not know the service yet, blocks new connections,
        #   or is not up yet
        # - ORA-03113 end-of-file on communication channel: the listener accepted the connection and the server
        #   process behind it was not ready (the container is still starting, or the database is being opened):
        #   CI jobs failed about a minute after the service start with exactly this error and passed on a retry
        if ! echo "$ORACLE_WAIT_OUT" | grep -E "QoreOracleConnection::logon.*ORA-(01017|01033|01034|01089|01109|03113|12514|12528|12541)" > /dev/null 2>&1; then
            echo && echo "ERROR: the Oracle DB reported an error that it does not report while it starts."
            return 3
        fi
        if [ $waited -eq $max_tries ]; then
            echo && echo "ERROR: the Oracle DB did not start within ${SECONDS} seconds."
            if [ -z "$listener_seen" ]; then
                echo "The listener never answered: the service container did not start, or exited before the first attempt."
            fi
            if [ -n "$in_ci" ]; then
                echo "The output of the service container is in this job's log (CI_DEBUG_SERVICES)."
            fi
            return 2
        fi

        printf "."
        sleep $sleep_secs
        waited=$((waited+1))
    done
}
