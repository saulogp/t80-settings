#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Remove a correção HID-BPF do Thrustmaster T80.
# As dependências de compilação (apt) NÃO são removidas; veja o README.

set -euo pipefail

PREFIX="/usr/local"
BPF_OBJ="/etc/udev-hid-bpf/0010-Thrustmaster__T80.bpf.o"
RULE="/etc/udev/rules.d/99-hid-bpf-0010-Thrustmaster__T80.rules"
EXE="$PREFIX/bin/udev-hid-bpf"
WORK_DIR="${WORK_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/t80-settings}"

info() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m ok\033[0m %s\n' "$*"; }

[ "$(id -u)" -ne 0 ] || { echo "rode como usuário normal (sem sudo)." >&2; exit 1; }

# Descarrega a correção de um volante conectado (o kernel relê o descritor original).
shopt -s nullglob
for dev in /sys/bus/hid/devices/*:044F:B667.*; do
    if [ -x "$EXE" ]; then
        info "Removendo a correção de $(basename "$dev")..."
        sudo "$EXE" remove "$dev" || true
    fi
done

info "Removendo arquivos instalados (sudo)..."
sudo rm -fv "$BPF_OBJ" "$RULE"
sudo rmdir --ignore-fail-on-non-empty /etc/udev-hid-bpf 2>/dev/null || true

# Só remove o executável se nenhuma outra correção HID-BPF depender dele.
others=(/etc/udev/rules.d/*hid-bpf*.rules /etc/udev-hid-bpf/*.bpf.o)
if [ ${#others[@]} -eq 0 ]; then
    sudo rm -fv "$EXE"
else
    info "Mantendo $EXE: há outras correções HID-BPF instaladas:"
    printf '      %s\n' "${others[@]}"
fi

sudo udevadm control --reload
rm -rf "$WORK_DIR"

# Visualizador gráfico (instalado só para o usuário, sem sudo)
rm -fv "$HOME/.local/bin/t80-viewer" "$HOME/.local/share/applications/io.github.saulogp.T80Viewer.desktop"

ok "Correção removida. Se o volante estiver conectado e os pedais ainda aparecerem, reconecte-o."
