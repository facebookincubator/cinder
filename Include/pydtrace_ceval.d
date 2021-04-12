/* Python DTrace provider for calls in Python/ceval.c */

provider python {
    probe function__entry(const char *, const char *, int);
    probe function__return(const char *, const char *, int);
    probe instance__new__start(const char *, const char *);
    probe instance__new__done(const char *, const char *);
    probe instance__delete__start(const char *, const char *);
    probe instance__delete__done(const char *, const char *);
    probe line(const char *, const char *, int);
    probe audit(const char *, void *);
    probe instruction(uint64_t, uint64_t, uint16_t, uint16_t*, uint16_t*);
    probe types(uint64_t, const char*, const char*, int, int, const char*, const char*);
};

#pragma D attributes Evolving/Evolving/Common provider python provider
#pragma D attributes Evolving/Evolving/Common provider python module
#pragma D attributes Evolving/Evolving/Common provider python function
#pragma D attributes Evolving/Evolving/Common provider python name
#pragma D attributes Evolving/Evolving/Common provider python args
