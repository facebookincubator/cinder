#!/bin/bash

PrintHelp() {
    echo ""
    echo "This script will run a benchmark and store it results in csv files."
    echo "'results' folder will be created a and inside it two csv files:"
    echo "  * description.csv: This file will contain the name of the experiments and the used comandline to execute it"
    echo "                      e.g.: Caption, Command"
    echo "                            Nqueens_static(), ./python Tools/benchmarks/nqueens_static.py 5"
    echo "                            Nqueens_static(SP), ./python -X install-strict-loader Tools/benchmarks/nqueens_static.py 5"
    echo "  * times.csv: Each column of this file will be time executions of one experiment, and number of rows correspond to the"
    echo "               number of runs"
    echo "                      e.g.: Nqueens_static(), Nqueens_static(SP)"
    echo "                            23.4, 13.2"
    echo "                            25.2, 12.8"
    echo "usage:"
    echo "  ./run-benchmarks.sh -file PYTHON_FILE_PATH -caption CAPTION -N 10 -R 3 -sp -jit JITLIST_FILE_PATH -sf"
    echo "  flags:"
    echo "      -file PYTHON_FILE_PATH -> Benchmark that will be executed"
    echo "      -N -> number of iterations of the benchmarck"
    echo "      -R -> number of runs"
    echo "      -sp -> install strict loader"
    echo "      -jit JITLIST_FILE_PATH ->  enable JIT (add the path to the jit list file as well)"
    echo "      -sf  -> enable jit shadow frame"
    echo "      -caption CAPTION -> Name of the experiment. Will be used in the headers of csv files"
    echo ""
}

flags=()
abb=()
R=1

validate () {
    if [ ! -f python ] || [[ ! $(./python --version ) = *cinder* ]]
    then
        echo ""
        echo "IMPORTANT: This script should be executed in the same folder as the cinder ./python executable"
        echo ""
        exit 1
    fi
    if [ -z "$file" ]
    then
        echo ""
        echo "ERROR: Benchmark file not given"
        echo ""
        exit 1
    fi
}

while test $# -gt 0; do
           case "$1" in
                -sp)
                    abb+=("SP")
                    flags+=("-X install-strict-loader")
                    shift
                    ;;
                -jit)
                    shift
                    abb+=("JIT")
                    flags+=("-X jit -X jit-list-file=$1 -X jit-enable-jit-list-wildcards")
                    shift
                    ;;
                -sf)
                    abb+=("SF")
                    flags+=("-X jit-shadow-frame")
                    shift
                    ;;
                -caption)
                    shift
                    caption=$1
                    shift
                    ;;
                -file)
                    shift
                    file=$1
                    shift
                    ;;
                -N)
                    shift
                    N=$1
                    shift
                    ;;
                -R)
                    shift
                    R=$1
                    shift
                    ;;
                -h|--help)
                    PrintHelp
                    exit 0
                    ;;
                *)
                   echo "$1 is not a recognized flag!"
                   exit
                   ;;
          esac
  done

validate

size=${#abb[@]}
iter=$[1<<$size]
captions=()
cmds=()

# Create results folder to store csv files
mkdir -p results
# Iterate the loop to read and print each array element
echo "Benchmark, Command" > results/description.csv
for (( mask=0; mask<iter; mask++ ))
do
    details=""
    flags_str=""
    for ((i=0; i<size; i++))
    do
        bit=$[ ($mask>>i) & 1 ]
        if [ $bit == 1 ];then
            if [ "$details" != "" ];then
                details+="-"
            fi
            details+=${abb[$i]}
            flags_str+=" ${flags[$i]}"
        fi

    done
    #check that SF can be valid if and only if JIT is enabled

    if [[ $details == *"SF"* && $details != *"JIT"* ]]; then
        continue
    fi
    captions+=("$caption($details)")
    cmds+=("./python$flags_str $file $N" )
    echo "$caption($details), ./python$flags_str $file $N">> results/description.csv
done
headers=""
for value in ${captions[@]}
do
    if [[ "$headers" != "" ]]; then
        headers+=","
    fi
    headers+=$value
done
echo $headers > results/times.csv
for ((t=0; t<$R; t++))
do
    row=""
    for ((i=0; i<${#cmds[@]}; i++))
    do
        cmd=${cmds[$i]}
        if [[ $i != 0 ]]; then
            row+=","
        fi
        echo $cmd
        out=`(\time -f "%e" $cmd) 2>&1 | tr ' ' '\n' | tail -1`
        echo $out
        row+=$out
    done
    echo $row >> results/times.csv
done
