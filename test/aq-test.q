#!/usr/bin/env qore

%modern
%requires oracle

const MSG_COUNT = 5;
const THREAD_COUNT = 5;

const CONNS = (
    "qube" : "omquser2/omquser2@qube",
    "stimpy" : "omquser/omquser@stimpy",
    "rimmer.local" : "omquser/omquser@xe%oraclexe11:1521",
    "vladkyne-karen" : "test/test@orcl%192.168.2.107:1521"
);
our string conn = CONNS{gethostname()};
printf("Hostname: %s; Connection: %s\n", gethostname(), conn);

our hash h;
our Counter c();

class MyAQQueue inherits AQQueue {
    constructor(string q1, string t1, string c1) : AQQueue(q1, t1, c1) {
    }

    log(string fmt) {
        printf("%s T%d async msg: %s\n", format_date("YYYY-MM-DD HH:mm:SS.xx", now_us()), gettid(), vsprintf(fmt, argv));
    }

    nothing onAsyncMessage() {
        *AQMessage m1;
        {
            on_success commit();
            on_error rollback();
            do {
                m1 = getMessage(1);
                log("%y", m1 ? m1.getObject() : NOTHING);
                if (m1) {
                    hash msg = m1.getObject();
                    delete h.(msg.CODE);
                    c.dec();
                    if (msg.CODE == 3003)
                        break;
                }
            } while (m1);
        }

        # do reconnect
        if (m1 && m1.getObject().CODE == 3003) {
            TerminalInputHelper tih();
            stdout.printf("push any key to continue the listening thread: ");
            stdout.sync();
            tih.getChar();
            stdout.printf("\n");
            stdout.sync();
        }
    }
}

MyAQQueue sub get_queue() {
    return new MyAQQueue("testaq", "test_aq_msg_obj", conn);
}

main();

sub main() {
    # first purge queue
    MyAQQueue q1 = get_queue();
    {
        on_success q1.commit();
        on_error q1.rollback();

        printf("purging queue...\n");
        while ((*AQMessage dm = q1.getMessage())) {
            printf("purging: msg: %y (%y)\n", dm, dm.getObject());
        }
    }

    q1.startSubscription();

    Counter sc();
    for (int i = 0; i < THREAD_COUNT; ++i) {
        sc.inc();
        background test(i, sc);
    }
    # wait until all msgs have been posted to start waiting until the queue is empty
    sc.waitForZero();

    # wait until the queue is empty
    c.waitForZero(60s);
    printf("not received: %y\n", h.keys());
}

sub test(int i, Counter sc) {
    on_exit sc.dec();

    #AQQueue q("omquser.testaq", "test_aq_msg_obj", conn);
    #AQQueue q("OMQUSER.TESTAQ", "TEST_AQ_MSG_OBJ", conn);
    MyAQQueue q1 = get_queue();
    on_error q1.rollback();

    AQMessage m1();
    #printf("MSG> %N\n", m1);
    hash objin = ("MESSAGE" : "from qorus", "CODE" : i * 1000);

    for (int i = 0; i < MSG_COUNT; ++i) {
        h.(objin.CODE) = True;
        m1.setObject(bindOracleObject("test_aq_msg_obj", objin));
        c.inc();
        q1.post(m1);
        q1.commit();
        ++objin.CODE;
    }
}

class TerminalInputHelper {
    private {
        # saved original terminal attributes
        TermIOS orig = stdin.getTerminalAttributes();

        # input terminal attributes
        TermIOS input;

        # restore flag
        bool rest = False;
    }
    
    public {}

    constructor() {
        input = orig.copy();

        # get local flags
        int lflag = input.getLFlag();

        # turn on "raw" mode: disable canonical input mode
        lflag &= ~ICANON;

        # turn off echo mode
        lflag &= ~ECHO;

        # set the new local flags
        input.setLFlag(lflag);

        # turn off input buffering: set minimum characters to return on a read
        input.setCC(VMIN, 1);

        # disable input timer: set character input timer to 0.1 second increments
        input.setCC(VTIME, 0);
    }

    destructor() {
        if (rest)
            restore();
    }

    *string getLine() {
        # set input attributes
        stdin.setTerminalAttributes(TCSADRAIN, input);
        rest = True;

        # restore attributes on exit
        on_exit {
            stdin.setTerminalAttributes(TCSADRAIN, orig);
            rest = False;
            stdout.print("\n");
        }

        return stdin.readLine(False);
    }

    *string getChar() {
        # set input attributes
        stdin.setTerminalAttributes(TCSADRAIN, input);
        rest = True;

        # restore attributes on exit
        on_exit {
            stdin.setTerminalAttributes(TCSADRAIN, orig);
            rest = False;
        }

        return stdin.read(1);
    }

    restore() {
        # restore terminal attributes
        stdin.setTerminalAttributes(TCSADRAIN, orig);
    }
}
