#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Diagnóstico do Thrustmaster T80 no Linux. Não precisa de root.
#
#   ./check.sh            mostra modo, estado da correção e eixos
#   ./check.sh --test [N] também captura N segundos (padrão 20) de entrada:
#                         gire o volante e pise nos pedais enquanto roda

set -uo pipefail

ok()   { printf '\033[1;32m ok\033[0m %s\n' "$*"; }
bad()  { printf '\033[1;31m XX\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m !!\033[0m %s\n' "$*"; }
hdr()  { printf '\n\033[1m%s\033[0m\n' "$*"; }

hdr "Arquivos instalados"
for f in /usr/local/bin/udev-hid-bpf \
         /etc/udev-hid-bpf/0010-Thrustmaster__T80.bpf.o \
         /etc/udev/rules.d/99-hid-bpf-0010-Thrustmaster__T80.rules; do
    if [ -e "$f" ]; then ok "$f"; else bad "$f (ausente; rode ./install.sh)"; fi
done

hdr "Volante"
shopt -s nullglob
ps3=(/sys/bus/hid/devices/*:044F:B667.*)
ps4=(/sys/bus/hid/devices/*:044F:B668.*)
other=(/sys/bus/hid/devices/*:044F:*)

if [ ${#ps3[@]} -eq 0 ]; then
    if [ ${#ps4[@]} -gt 0 ]; then
        bad "T80 conectado em modo PS4 (044f:b668): volante e pedais não funcionam nesse modo."
        echo "     Mude a chave lateral para PS3 e reconecte o USB."
    elif [ ${#other[@]} -gt 0 ]; then
        warn "dispositivo Thrustmaster encontrado, mas não é o T80 em modo PS3:"
        for d in "${other[@]}"; do echo "     $(basename "$d")"; done
    else
        bad "T80 não encontrado. Conecte o USB (chave em PS3)."
    fi
    exit 1
fi

dev="${ps3[0]}"
ok "T80 em modo PS3: $(basename "$dev")"

size="$(wc -c < "$dev/report_descriptor")"
case "$size" in
    146) ok  "correção ATIVA (descritor de 146 bytes)" ;;
    137) bad "correção INATIVA (descritor original de 137 bytes)."
         echo "     Reconecte o volante; se continuar, veja: journalctl -b | grep -i hid-bpf" ;;
    *)   warn "descritor com tamanho inesperado: $size bytes" ;;
esac

evdev=""
for e in "$dev"/input/input*/event*; do evdev="/dev/input/$(basename "$e")"; done
[ -n "$evdev" ] || { bad "nenhum dispositivo /dev/input/event* associado."; exit 1; }
ok "dispositivo de entrada: $evdev"
[ -r "$evdev" ] || { bad "sem permissão de leitura em $evdev"; exit 1; }

hdr "Eixos"
TEST_SECS=0
if [ "${1:-}" = "--test" ]; then TEST_SECS="${2:-20}"; fi

python3 -I - "$evdev" "$TEST_SECS" <<'EOF'
import fcntl, os, select, struct, sys, time

path, secs = sys.argv[1], float(sys.argv[2])
EVIOCGABS = lambda code: 0x80184540 + code
names = {0: "ABS_X", 1: "ABS_Y", 2: "ABS_Z", 3: "ABS_RX", 4: "ABS_RY", 5: "ABS_RZ",
         16: "ABS_HAT0X", 17: "ABS_HAT0Y"}
role = {"ABS_X": "volante", "ABS_RX": "pedal esquerdo (freio)",
        "ABS_RY": "pedal direito (acelerador)", "ABS_HAT0X": "direcional",
        "ABS_HAT0Y": "direcional"}

fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
present = {}
for code, name in names.items():
    buf = bytearray(24)
    try:
        fcntl.ioctl(fd, EVIOCGABS(code), buf)
    except OSError:
        continue
    val, mn, mx, fuzz, flat, res = struct.unpack("6i", buf)
    if mn == mx == 0:
        continue
    present[name] = (mn, mx)
    print(f"  {name:10} {mn:4}..{mx:<4} atual={val:<4} {role.get(name, '')}")

for p in ("ABS_RX", "ABS_RY"):
    if p not in present:
        print(f"\n  XX {p} ausente: os pedais não aparecem como eixos (correção inativa?)")

if secs <= 0:
    sys.exit(0)

print(f"\nCapturando por {secs:.0f}s: gire o volante e pise devagar em cada pedal...")
seen, btns, end = {}, {}, time.time() + secs
while time.time() < end:
    r, _, _ = select.select([fd], [], [], 0.25)
    if not r:
        continue
    try:
        data = os.read(fd, 24 * 64)
    except BlockingIOError:
        continue
    for i in range(0, len(data) - 23, 24):
        _, _, typ, code, val = struct.unpack("qqHHi", data[i:i + 24])
        if typ == 3 and code in names:
            n = names[code]
            lo, hi, cnt = seen.get(n, (val, val, 0))
            seen[n] = (min(lo, val), max(hi, val), cnt + 1)
        elif typ == 1 and val == 1:
            btns[hex(code)] = btns.get(hex(code), 0) + 1

print("\nResultado:")
for n in ("ABS_X", "ABS_RX", "ABS_RY"):
    if n in seen:
        lo, hi, cnt = seen[n]
        status = "ok" if hi - lo >= 200 else "parcial"
        print(f"  {n:7} {lo:4}..{hi:<4} {cnt:5} eventos  [{status}]  {role[n]}")
    else:
        print(f"  {n:7} sem movimento                 {role[n]}")
print("  botões pressionados:", ", ".join(f"{k} x{v}" for k, v in sorted(btns.items())) or "nenhum")
EOF
