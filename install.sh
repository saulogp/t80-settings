#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Instala a correção HID-BPF dos pedais do Thrustmaster T80 (modo PS3, 044f:b667).
#
# O que este script faz:
#   1. Instala as dependências de compilação via apt (pede sudo).
#   2. Baixa o udev-hid-bpf (freedesktop.org) numa versão fixa e testada.
#   3. Compila somente o carregador e a correção do T80.
#   4. Instala o carregador, a correção e a regra do udev (pede sudo).
#   5. Aplica a correção na hora se o volante estiver conectado.
#   6. Compila e instala o visualizador gráfico (gui/) em ~/.local.
#
# Pode ser executado de novo sem problema (para atualizar ou reparar).
#
# Variáveis opcionais:
#   UHB_REF   commit/tag do udev-hid-bpf a usar (padrão: versão testada abaixo)
#   WORK_DIR  pasta de compilação (padrão: ~/.cache/t80-settings)
#   SKIP_APT=1    não instala dependências via apt
#   BUILD_ONLY=1  só compila (não instala nada; útil para testar)
#   SKIP_GUI=1    não compila/instala o visualizador gráfico

set -euo pipefail

UHB_REPO="${UHB_REPO:-https://gitlab.freedesktop.org/libevdev/udev-hid-bpf.git}"
# Versão testada em 2026-11-15 (udev-hid-bpf 2.3.0-20260703, kernel 7.2, Pop!_OS 24.04)
UHB_REF="${UHB_REF:-bbaad639d4b2efe2a909e8b7c594b91e87704ab8}"
WORK_DIR="${WORK_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/t80-settings}"

BPF_SRC_NAME="0010-Thrustmaster__T80.bpf.c"
BPF_OBJ_NAME="0010-Thrustmaster__T80.bpf.o"
PREFIX="/usr/local"
VID="044F"
PID="B667"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BPF_SRC="$SCRIPT_DIR/bpf/$BPF_SRC_NAME"
UHB_DIR="$WORK_DIR/udev-hid-bpf"

info()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
ok()    { printf '\033[1;32m ok\033[0m %s\n' "$*"; }
warn()  { printf '\033[1;33m !!\033[0m %s\n' "$*" >&2; }
die()   { printf '\033[1;31merro:\033[0m %s\n' "$*" >&2; exit 1; }

version_ge() { [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" = "$2" ]; }

# ---------------------------------------------------------------------------
# Verificações iniciais
# ---------------------------------------------------------------------------
[ "$(id -u)" -ne 0 ] || die "rode como usuário normal (sem sudo); o script pede sudo só quando precisa."
[ -f "$BPF_SRC" ] || die "não encontrei $BPF_SRC"
command -v apt-get >/dev/null || [ "${SKIP_APT:-0}" = 1 ] || \
    die "este script usa apt (Pop!_OS/Ubuntu/Debian). Em outra distro, instale as dependências manualmente e rode com SKIP_APT=1."

KVER="$(uname -r)"
version_ge "${KVER%%-*}" "6.11" || \
    die "kernel $KVER é antigo demais: é preciso 6.11 ou mais novo (HID-BPF struct_ops)."

if [ -r "/boot/config-$KVER" ] && ! grep -q '^CONFIG_HID_BPF=y' "/boot/config-$KVER"; then
    die "o kernel $KVER não tem CONFIG_HID_BPF=y."
fi

if [ -r /sys/kernel/security/lockdown ] && grep -q '\[confidentiality\]' /sys/kernel/security/lockdown; then
    warn "kernel em lockdown 'confidentiality' (Secure Boot); o carregamento de BPF pode ser bloqueado."
fi

# ---------------------------------------------------------------------------
# 1. Dependências
# ---------------------------------------------------------------------------
if [ "${SKIP_APT:-0}" != 1 ]; then
    info "Instalando dependências de compilação (sudo apt)..."
    pkgs=(git clang llvm libbpf-dev meson ninja-build libudev-dev libelf-dev zlib1g-dev pkg-config cargo rustc)
    [ "${SKIP_GUI:-0}" = 1 ] || pkgs+=(make gcc libgtk-4-dev)
    if apt-cache show bpftool >/dev/null 2>&1; then
        pkgs+=(bpftool)
    else
        pkgs+=(linux-tools-common "linux-tools-$KVER")
    fi
    sudo apt-get update
    sudo apt-get install -y "${pkgs[@]}"
fi

# Prefere o Rust do rustup, se existir (útil quando o cargo do apt é antigo).
[ -x "$HOME/.cargo/bin/cargo" ] && export PATH="$HOME/.cargo/bin:$PATH"

for tool in git clang meson ninja cargo pkg-config; do
    command -v "$tool" >/dev/null || die "ferramenta '$tool' não encontrada."
done
command -v bpftool >/dev/null || [ -x /usr/sbin/bpftool ] || die "bpftool não encontrado."

CARGO_VER="$(cargo --version | awk '{print $2}')"
version_ge "${CARGO_VER%%-*}" "1.78" || \
    die "cargo $CARGO_VER é antigo (precisa >= 1.78). Instale o Rust via https://rustup.rs e rode de novo."

# ---------------------------------------------------------------------------
# 2. Código-fonte do udev-hid-bpf
# ---------------------------------------------------------------------------
info "Baixando udev-hid-bpf ($UHB_REF) em $UHB_DIR..."
mkdir -p "$WORK_DIR"
rm -rf "$UHB_DIR"
git clone --quiet --filter=blob:none "$UHB_REPO" "$UHB_DIR"
git -C "$UHB_DIR" -c advice.detachedHead=false checkout --quiet "$UHB_REF"
ok "udev-hid-bpf $(git -C "$UHB_DIR" log -1 --format='%h (%cs)')"

# Adiciona a correção do T80 à árvore e registra no meson.
TESTING_DIR="$UHB_DIR/src/bpf/testing"
cp "$BPF_SRC" "$TESTING_DIR/$BPF_SRC_NAME"
if ! grep -q "'$BPF_SRC_NAME'" "$TESTING_DIR/meson.build"; then
    sed -i "/^tracing_sources = \[/a\\    '$BPF_SRC_NAME'," "$TESTING_DIR/meson.build"
fi
grep -q "'$BPF_SRC_NAME'" "$TESTING_DIR/meson.build" || \
    die "não consegui registrar $BPF_SRC_NAME no meson.build (o udev-hid-bpf mudou?)."

# ---------------------------------------------------------------------------
# 3. Compilação (só o necessário)
# ---------------------------------------------------------------------------
info "Configurando a compilação..."
meson setup "$UHB_DIR/builddir" "$UHB_DIR" \
    --prefix="$PREFIX" -Dbuildtype=release -Dbpf-filter=Thrustmaster__T80 >/dev/null

# Compila só o carregador e a correção do T80. Compilar "tudo" pode falhar por
# causa de correções de outros dispositivos que exigem um clang mais novo.
info "Compilando o carregador e a correção (pode levar alguns minutos)..."
ninja -C "$UHB_DIR/builddir" udev-hid-bpf "src/bpf/$BPF_OBJ_NAME"

UHB_BIN="$UHB_DIR/builddir/udev-hid-bpf"
BPF_OBJ="$UHB_DIR/builddir/src/bpf/$BPF_OBJ_NAME"
[ -x "$UHB_BIN" ] && [ -f "$BPF_OBJ" ] || die "a compilação não gerou os arquivos esperados."
"$UHB_BIN" inspect "$BPF_OBJ" | grep -qi "\"pid\": \"0x$PID\"" || \
    die "o objeto compilado não parece ser a correção do T80."
ok "compilado: $("$UHB_BIN" --version)"

if [ "${BUILD_ONLY:-0}" = 1 ]; then
    ok "BUILD_ONLY=1: parando aqui. Arquivos em $UHB_DIR/builddir"
    exit 0
fi

# ---------------------------------------------------------------------------
# 4. Instalação
# ---------------------------------------------------------------------------
info "Instalando (sudo)..."
# Instala/atualiza o executável explicitamente (o --install-exe não sobrescreve).
sudo install -D -m 0755 "$UHB_BIN" "$PREFIX/bin/udev-hid-bpf"
sudo "$UHB_BIN" install --prefix "$PREFIX" --force "$BPF_OBJ" >/dev/null
sudo udevadm control --reload
ok "arquivos instalados:"
printf '      %s\n' \
    "$PREFIX/bin/udev-hid-bpf" \
    "/etc/udev-hid-bpf/$BPF_OBJ_NAME" \
    "/etc/udev/rules.d/99-hid-bpf-${BPF_OBJ_NAME%.bpf.o}.rules"

# ---------------------------------------------------------------------------
# 5. Aplica agora, se o volante estiver conectado
# ---------------------------------------------------------------------------
shopt -s nullglob
devs=(/sys/bus/hid/devices/*:"$VID":"$PID".*)
if [ ${#devs[@]} -eq 0 ]; then
    if compgen -G "/sys/bus/hid/devices/*:$VID:B668.*" >/dev/null; then
        warn "o T80 está conectado em modo PS4 (044f:b668). Mude a chave para PS3 e reconecte."
    else
        info "T80 (modo PS3) não está conectado. Ao conectar, a correção será aplicada sozinha."
    fi
else
    for dev in "${devs[@]}"; do
        size="$(wc -c < "$dev/report_descriptor")"
        if [ "$size" -eq 137 ]; then
            info "Aplicando a correção em $(basename "$dev")..."
            sudo "$PREFIX/bin/udev-hid-bpf" add "$dev" "/etc/udev-hid-bpf/$BPF_OBJ_NAME"
        elif [ "$size" -eq 146 ]; then
            ok "correção já ativa em $(basename "$dev")."
        else
            warn "descritor inesperado ($size bytes) em $(basename "$dev"); reconecte o volante."
        fi
    done
fi

# ---------------------------------------------------------------------------
# 6. Visualizador gráfico (opcional, instalado só para o usuário)
# ---------------------------------------------------------------------------
if [ "${SKIP_GUI:-0}" != 1 ]; then
    info "Compilando e instalando o visualizador (gui/)..."
    if pkg-config --exists gtk4; then
        make -C "$SCRIPT_DIR/gui" --no-print-directory clean all
        make -C "$SCRIPT_DIR/gui" --no-print-directory install >/dev/null
        ok "visualizador instalado: ~/.local/bin/t80-viewer (menu: \"Visualizador T80\")"
    else
        warn "gtk4 (libgtk-4-dev) não encontrado; pulando o visualizador."
    fi
fi

echo
ok "Pronto. Confira com: $SCRIPT_DIR/check.sh   (ou '$SCRIPT_DIR/check.sh --test' para testar os pedais)"
