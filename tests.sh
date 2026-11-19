#!/bin/sh
set -eu

bin=$(cd "$1" && pwd)
mode=${2:-}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/lab3.XXXXXXXX")
trap 'rm -rf "$tmp"' 0
trap 'exit 1' HUP INT TERM

run() {
    program=$1
    shift
    if [ "$mode" = "--valgrind" ]; then
        valgrind --quiet --error-exitcode=97 --leak-check=full \
            --show-leak-kinds=all --errors-for-leak-kinds=all "$bin/$program" "$@"
    else
        "$bin/$program" "$@"
    fi
}

expect() {
    if [ "$1" != "$2" ]; then
        printf 'Lab %s failed\nExpected: %s\nActual: %s\n' "$3" "$2" "$1" >&2
        exit 1
    fi
}

out=$(run lab01 -255 4)
expect "$out" "-FF" 01
out=$(run lab02 <<'DATA'
4 2
1 09:00 10:00
2 09:30 10:30
3 09:30 11:00
4 10:00 11:00
DATA
)
expect "$out" 3 02

printf 'AAAAAAAAA' > "$tmp/bytes"
out=$(run lab03 "$tmp/bytes")
expect "$out" '65 9 0
original_bytes 9
encoded_bits 9
encoded_bytes 2
savings_percent 77.777778' 03

out=$(run lab04 string)
expect "$out" 'order=-1 equal=1 concat=catcat copy=cat clone=cat' 04
out=$(run lab04 <<'DATA'
Town|Main|1||1|123456
add Town|West|2||1|123457|0.5|00000000000001|01:01:2000 10:00:00|02:01:2000 10:00:00
find 00000000000001
deliver 00000000000001
delivered
remove 00000000000001
quit
DATA
)
expect "$(printf '%s\n' "$out" | awk '/^0000/ {print $NF}')" 'pending
delivered' 04

cat > "$tmp/students" <<'DATA'
2 Anna White G2 5 5 5 5 5
1 Mark Black G1 3 3 3 3 3
DATA
run lab05 "$tmp/students" "$tmp/trace" <<'DATA' >/dev/null
sort id
find group G1
trace 2
above
quit
DATA
expect "$(cat "$tmp/trace")" 'White Anna G2 5.00
White Anna' 05

cat > "$tmp/start" <<'DATA'
0 0
Bus 19.11.2026 09:00:00 19.11.2026 09:00:30 S
DATA
cat > "$tmp/finish" <<'DATA'
3 4
Bus 19.11.2026 09:10:00 19.11.2026 09:10:10 F
DATA
out=$(run lab06 "$tmp/finish" "$tmp/start" <<'DATA'
routes-max
route-max
idle-max
quit
DATA
)
expect "$(printf '%s\n' "$out" | tail -n 3)" 'Bus 1
Bus 5
Bus 40' 06

printf 'Ivanov Ivan - 10 05 1980 M 700\n' > "$tmp/residents"
run lab07 "$tmp/residents" <<DATA >/dev/null
add Petrova Anna - 29 02 2004 W 900
remove 1
undo
save $tmp/residents-out
quit
DATA
expect "$(cat "$tmp/residents-out")" 'Petrova Anna - 29 02 2004 W 900
Ivanov Ivan - 10 05 1980 M 700' 07

printf 'Mult(x+1,x-1); Eval(2); Diff(); Cmps(x+1,x-1);\n' > "$tmp/poly"
out=$(run lab08 "$tmp/poly")
expect "$out" 'x^2-1
3
2x
x' 08

printf 'pear,apple,pear,plum' > "$tmp/words"
out=$(run lab09 "$tmp/words" , <<DATA
count pear
depth
save $tmp/tree
load $tmp/tree
save $tmp/restored
depth
quit
DATA
)
expect "$(printf '%s\n' "$out" | tail -n 3)" '2
2
2' 09
cmp "$tmp/tree" "$tmp/restored"

printf 'A(B(D),C)\n' > "$tmp/expressions"
run lab10 "$tmp/expressions" "$tmp/pretty"
expect "$(cat "$tmp/pretty")" 'Tree 1
A
  B
    D
  C' 10

printf 'All 10 labs passed\n'
