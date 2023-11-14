import sys, os, subprocess, signal

programs = [
    'stl_unordered_map',
    'abseil_flat',
    'abseil_parallel_flat'
]

test_size   = 1
minkeys     = 0
maxkeys     = 1000
interval    = 100
best_out_of = 1

if test_size == 2:
    multiplier = 1000 * 1000
    maxkeys  = 500
    best_out_of = 3
    
elif test_size == 1:
    multiplier = 100 * 1000
    interval = 200
else:
    multiplier = 10 * 1000

# and use nice/ionice
# and shut down to the console
# and swapoff any swap files/partitions

outfile = open('output', 'w')

if len(sys.argv) > 1:
    benchtypes = sys.argv[1:]
else:
    benchtypes = ( 'random', 'lookup', 'delete',)
    #benchtypes = (  'lookup', )

for benchtype in benchtypes:
    nkeys = minkeys * multiplier
    while nkeys <= (maxkeys * multiplier):
        for program in programs:
            fastest_attempt = 1000000
            fastest_attempt_data = ''

            for attempt in range(best_out_of):
                proc = subprocess.Popen(['./build/'+program, str(nkeys), benchtype], stdout=subprocess.PIPE)

                # wait for the program to fill up memory and spit out its "ready" message
                try:
                    runtime = float(proc.stdout.readline().strip())
                except:
                    runtime = 0

                ps_proc = subprocess.Popen(['ps up %d | tail -n1' % proc.pid], shell=True, stdout=subprocess.PIPE)
                #nbytes = int(ps_proc.stdout.read().split()[4]) * 1024
                #ps_proc.wait()
                nbytes = 1000000

                os.kill(proc.pid, signal.SIGKILL)
                proc.wait()

                if nbytes and runtime: # otherwise it crashed
                    line = ','.join(map(str, [benchtype, nkeys, program, nbytes, "%0.6f" % runtime]))

                    if runtime < fastest_attempt:
                        fastest_attempt = runtime
                        fastest_attempt_data = line

            if fastest_attempt != 1000000:
                print >> outfile, fastest_attempt_data
                print fastest_attempt_data

        nkeys += interval * multiplier
