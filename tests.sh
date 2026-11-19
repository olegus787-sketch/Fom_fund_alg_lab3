#!/bin/sh
set -eu

build=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' 0

# Один и тот же пример можно запустить обычным способом или через Valgrind.
run() {
    program=$1
    shift
    if [ "${CHECK_MEMORY:-0}" = 1 ]; then
        valgrind --quiet --error-exitcode=97 --leak-check=full \
            --show-leak-kinds=all --errors-for-leak-kinds=all "$build/$program" "$@"
    else
        "$build/$program" "$@"
    fi
}

# Ищем ожидаемую строку в результате последнего запуска.
expected_line() {
    if ! printf '%s\n' "$output" | grep -Fxq -- "$1"; then
        printf '%s: ожидалось «%s»\nПолучено:\n%s\n' "$program_name" "$1" "$output" >&2
        exit 1
    fi
}

program_name=lab01
output=$(run lab01 31 4)
expected_line '1F'
echo '01 OK'

# Два зала. Три доклада можно провести, четвёртый пересекается с выбранными.
program_name=lab02
output=$(run lab02 <<'DATA'
4 2
1 09:00 10:00
2 09:30 10:30
3 09:30 11:00
4 10:00 11:00
DATA
)
expected_line '3'
echo '02 OK'

program_name=lab03
printf 'AAAAAAAAA' > "$tmp/bytes"
output=$(run lab03 "$tmp/bytes")
expected_line '65 9 0'
expected_line 'encoded_bytes 2'
echo '03 OK'

program_name=lab04
output=$(run lab04 string)
expected_line 'order=-1 equal=1 concat=catcat copy=cat clone=cat'
output=$(run lab04 <<'DATA'
Town|Main|1||1|123456
add Town|West|2||1|123457|0.5|00000000000001|01:01:2000 10:00:00|02:01:2000 10:00:00
find 00000000000001
deliver 00000000000001
delivered
remove 00000000000001
quit
DATA
)
expected_line '00000000000001 | Town, West, 2, apt 1 | 123457 | 0.5 | 01:01:2000 10:00:00 | 02:01:2000 10:00:00 | delivered'
echo '04 OK'

program_name=lab05
printf '%s\n' '2 Anna White G2 5 5 5 5 5' '1 Mark Black G1 3 3 3 3 3' > "$tmp/students"
printf 'sort id\nfind group G1\ntrace 2\nabove\nquit\n' | run lab05 "$tmp/students" "$tmp/trace" >/dev/null
output=$(cat "$tmp/trace")
expected_line 'White Anna G2 5.00'
expected_line 'White Anna'
echo '05 OK'

program_name=lab06
printf '0 0\nBus 19.11.2026 09:00:00 19.11.2026 09:00:30 S\n' > "$tmp/start"
printf '3 4\nBus 19.11.2026 09:10:00 19.11.2026 09:10:10 F\n' > "$tmp/finish"
output=$(printf 'route-max\nidle-max\nquit\n' | run lab06 "$tmp/finish" "$tmp/start")
expected_line 'Bus 5'
expected_line 'Bus 40'
echo '06 OK'

program_name=lab07
printf 'Ivanov Ivan - 10 05 1980 M 700\n' > "$tmp/residents"
run lab07 "$tmp/residents" <<DATA >/dev/null
add Petrova Anna - 29 02 2004 W 900
remove 1
undo
save $tmp/residents-out
quit
DATA
output=$(cat "$tmp/residents-out")
expected_line 'Petrova Anna - 29 02 2004 W 900'
expected_line 'Ivanov Ivan - 10 05 1980 M 700'
echo '07 OK'

program_name=lab08
printf 'Mult(x+1,x-1); Eval(2); Diff();\n' > "$tmp/poly"
output=$(run lab08 "$tmp/poly")
expected_line 'x^2-1'
expected_line '3'
expected_line '2x'
echo '08 OK'

program_name=lab09
printf 'pear,apple,pear,plum' > "$tmp/words"
output=$(run lab09 "$tmp/words" , <<DATA
all
save $tmp/tree
load $tmp/tree
save $tmp/restored
quit
DATA
)
expected_line 'pear 2'
cmp "$tmp/tree" "$tmp/restored"
echo '09 OK'

program_name=lab10
printf 'A(B(D),C)\n' > "$tmp/expressions"
run lab10 "$tmp/expressions" "$tmp/pretty"
output=$(cat "$tmp/pretty")
expected_line '    D'
expected_line '  C'
echo '10 OK'
