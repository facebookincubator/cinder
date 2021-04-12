/* Python DTrace provider for calls in Modules/gcmodule.c */

provider python {
    probe gc__start(int);
    probe gc__done(long);
};

#pragma D attributes Evolving/Evolving/Common provider python provider
#pragma D attributes Evolving/Evolving/Common provider python module
#pragma D attributes Evolving/Evolving/Common provider python function
#pragma D attributes Evolving/Evolving/Common provider python name
#pragma D attributes Evolving/Evolving/Common provider python args
