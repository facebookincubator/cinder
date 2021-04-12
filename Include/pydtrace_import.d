/* Python DTrace provider for calls in Python/import.c */

provider python {
    probe import__find__load__start(const char *);
    probe import__find__load__done(const char *, int);
};

#pragma D attributes Evolving/Evolving/Common provider python provider
#pragma D attributes Evolving/Evolving/Common provider python module
#pragma D attributes Evolving/Evolving/Common provider python function
#pragma D attributes Evolving/Evolving/Common provider python name
#pragma D attributes Evolving/Evolving/Common provider python args
