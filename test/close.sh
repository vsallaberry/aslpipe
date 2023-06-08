#!/bin/bash
mydir=$(cd "$(dirname "$0")"; pwd)

aslpipe="${mydir}/../aslpipe"

check_proc() {
    local f err=0
    for f in "$@"; do
        local command=$(ps -o pid=,pgid=,state=,command= "$f");
        local state=$(printf "$command" | awk '{print $3}')
        case "$state" in
            T*) state=stopped;;
            S*) state=sleeping;;
            R*) state=running;;
            *) state='??';;
        esac
        test -n "${command}"  \
            && { echo "$state ($command)" 1>&2; echo "$state ${command}"; } \
            || { echo "NOT running." 1>&2; err=$((err+1)); echo "missing"; }
    done
    return $err
}

dotest() {
    local mode=$1
    local err=0
    printf -- "\n** TEST START, mode $mode\n"

    "${aslpipe}" -K MessageV "${mydir}/close" & eval 'local pid=$!'
    sleep 1

    # check aslpipe on ON
    printf -- '+ aslpipe: ' 1>&2;
    local command=$(check_proc "$pid")

    # get its children
    local pgid=$(ps -o pgid= $pid)
    local child=$(pgrep -g $pgid)
    child=$(printf -- "$child" | tail -n1)
    printf -- '+ child: '; check_proc "${child}" > /dev/null

    printf -- "+ aslpipe: "; check_proc "${pid}" | grep -Eq 'sleeping|running' || { echo "!! not ON"; err=$((err+1)); }
    printf -- '+ child: '; check_proc "${child}" | grep -Eq 'sleeping|running' || { echo "!! not ON"; err=$((err+1)); }

    local sig=TSTP
    local target=$pid
    local aslstate='stopped'
    local childstate='stopped'
    test $mode -ge 3 -a $mode -le 4 && sig=STOP && childstate='sleeping|running'
    test $mode -ge 5 -a $mode -le 6 && target=$child
    test $mode -ge 7 -a $mode -le 8 && sig=STOP && target=$child

    case "$target" in $pid) targetname=aslpipe;; $child)targetname=child;; *)targetname="??";; esac

    echo "+ $sig to $targetname..."
    kill "-${sig}" $target
    printf -- "  aslpipe: "; check_proc "${pid}" | grep -Eq "${alsstate}" || { echo "  !! not $aslstate"; err=$((err+1)); }
    printf -- '  child: '; check_proc "${child}" | grep -Eq "${childstate}" || { echo "  !! not $childstate"; err=$((err+1)); }

    if test "$mode" != 7; then
        echo "+ CONT to $targetname..."
        kill -CONT $target
        printf -- "  aslpipe: "; check_proc "${pid}" | grep -Eq 'sleeping|running' || { echo "  !! not ON"; err=$((err+1)); }
        printf -- '  child: '; check_proc "${child}" | grep -Eq 'sleeping|running' || { echo "  !! not ON"; err=$((err+1)); }
    fi

    case "$mode" in
        1) echo "+ kill aslpipe..."; kill "$pid";;
        2) echo "+ kill child...";   kill "$child";;
        3) echo "+ kill child...";   kill "$child";;
        4) echo "+ kill aslpipe..."; kill "$pid";;
        5) echo "+ kill child...";   kill "$child";;
        6) echo "+ kill aslpipe..."; kill "$pid";;
        7) echo "+ kill child...";   kill "$child";;
        8) echo "+ kill aslpipe..."; kill "$pid";;
    esac

    check_proc "${pid}" "${child}" > /dev/null; local ret=$?
    test "$ret" -eq 2 || { err=$((err+1)); echo "  error ($ret): process running"; }

    printf -- "$err error(s)\n"
    return $err
}

test -x "${CC}" || CC=$(which clang gcc cc | head -n1)

test -x "${mydir}/close" -a \! "${mydir}/close.c.out" -nt "${mydir}/close" \
    || { echo "+ building close with '${CC}'..."; "${CC}" -x c -o "${mydir}/close" "${mydir}/close.c.out" || exit 1; }

err=0
for ((i=1; i<=8; i=i+1)); do
    dotest $i || { err=$((err+$?)); }
done
printf -- "\n-> $err error(s)\n"

exit $err

