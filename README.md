# Thrustmaster T80 no Linux: pedais analógicos

Correção para fazer o volante **Thrustmaster T80** funcionar por completo no Linux,
incluindo **acelerador e freio analógicos**. Sem ela, o Linux só enxerga os pedais
como botões liga/desliga.

A correção é um pequeno programa [HID-BPF](https://docs.kernel.org/hid/hid-bpf.html)
(o mecanismo oficial do kernel para corrigir descritores de dispositivos HID),
carregado automaticamente pelo [udev-hid-bpf](https://gitlab.freedesktop.org/libevdev/udev-hid-bpf)
sempre que o volante é conectado. Não é um driver de terceiros e não modifica o kernel.

| | Sem correção | Com correção |
|---|---|---|
| Volante | ✅ analógico (0–255) | ✅ analógico (0–255) |
| Botões / borboletas / direcional | ✅ | ✅ |
| Pedais | ⚠️ só liga/desliga | ✅ **analógicos (0–255)** |

## Instalação rápida

1. Coloque a **chave lateral do volante em PS3** e conecte o USB.
2. Rode:

```bash
git clone https://github.com/saulogp/t80-settings.git
cd t80-settings
./install.sh
```

O script pede a senha do `sudo` duas vezes: uma para instalar as dependências (apt)
e outra para instalar a correção. Ele pode ser rodado de novo quantas vezes quiser,
para atualizar ou reparar a instalação.

3. Confira:

```bash
./check.sh          # modo do volante, estado da correção e eixos
./check.sh --test   # captura 20 s: gire o volante e pise nos pedais
```

### Requisitos

- **Pop!_OS / Ubuntu / Debian** (o script usa `apt`). Em outra distro, instale as
  dependências equivalentes e rode `SKIP_APT=1 ./install.sh`.
- **Kernel 6.11 ou mais novo** com `CONFIG_HID_BPF=y` (padrão nas distros atuais).
- Testado em: Pop!_OS 24.04, kernel 7.2.15, udev-hid-bpf 2.3.0, em 2026-11-15.

### Opções do `install.sh`

| Variável | Efeito |
|---|---|
| `SKIP_APT=1` | não instala dependências via apt |
| `BUILD_ONLY=1` | só compila, não instala nada |
| `UHB_REF=<commit>` | usa outra versão do udev-hid-bpf (padrão: a versão testada) |
| `WORK_DIR=<pasta>` | onde compilar (padrão: `~/.cache/t80-settings`) |

## Mapeamento resultante

| Controle | Evento Linux | Faixa |
|---|---|---|
| Volante | `ABS_X` | 0 (esquerda) – 128 (centro) – 255 (direita) |
| Pedal esquerdo (freio) | `ABS_RX` | 0 (solto) – 255 (fundo) |
| Pedal direito (acelerador) | `ABS_RY` | 0 (solto) – 255 (fundo) |
| Direcional | `ABS_HAT0X` / `ABS_HAT0Y` | −1 – 1 |
| Botões e borboletas | `BTN_SOUTH` … `BTN_MODE` (13 botões) | |

Os eixos `ABS_Y`, `ABS_Z` e `ABS_RZ` existem no descritor, mas o T80 não os usa
(ficam parados em 128). Os pedais também continuam enviando `BTN_TL`/`BTN_TR`
(liga/desliga) junto do valor analógico. Na hora de mapear no jogo, use os eixos.

## Desinstalação

```bash
./uninstall.sh
```

Remove os arquivos instalados e descarrega a correção de um volante conectado.
As dependências de compilação não são removidas. Se quiser removê-las:

```bash
sudo apt remove clang llvm libbpf-dev bpftool meson ninja-build libudev-dev libelf-dev cargo rustc
```

### Arquivos instalados

| Arquivo | Para quê |
|---|---|
| `/usr/local/bin/udev-hid-bpf` | carregador de programas HID-BPF |
| `/etc/udev-hid-bpf/0010-Thrustmaster__T80.bpf.o` | a correção compilada |
| `/etc/udev/rules.d/99-hid-bpf-0010-Thrustmaster__T80.rules` | carrega a correção quando o T80 (`044f:b667`) é conectado |

A regra do udev só é ativada para o T80 em modo PS3. Nada roda no boot nem em
outros dispositivos.

### Remoção manual (sem o script)

```bash
sudo rm /usr/local/bin/udev-hid-bpf \
        /etc/udev-hid-bpf/0010-Thrustmaster__T80.bpf.o \
        /etc/udev/rules.d/99-hid-bpf-0010-Thrustmaster__T80.rules
sudo udevadm control --reload
# depois desconecte e reconecte o volante
```

## Solução de problemas

**O volante não aparece / `check.sh` diz modo PS4 (`044f:b668`)**
No modo PS4 o T80 se apresenta como um controle DualShock 4 e fica esperando a
autenticação do console: só as borboletas funcionam. Mude a chave lateral para
**PS3** e reconecte o USB. O ID correto é `044f:b667`.

**`check.sh` diz "correção INATIVA" (descritor de 137 bytes)**
1. Desconecte e reconecte o volante.
2. Veja o log: `journalctl -b | grep -i -E "hid-bpf|044F:B667"`
3. Confira se os 3 arquivos estão instalados (`check.sh` lista) e rode `./install.sh` de novo.
4. Para aplicar na mão (some ao desconectar):
   ```bash
   sudo udev-hid-bpf add /sys/bus/hid/devices/0003:044F:B667.* /etc/udev-hid-bpf/0010-Thrustmaster__T80.bpf.o
   ```

**O jogo não reconhece os pedais**
- Confira primeiro com `./check.sh --test` que `ABS_RX`/`ABS_RY` vão de 0 a 255.
- Mapeie os pedais pelas opções de controle do jogo, pisando no pedal quando ele
  pedir o eixo. Se ele pegar o botão em vez do eixo, pise devagar.
- Na Steam, se o jogo não detectar o volante, tente ativar ou desativar o Steam Input
  nas propriedades do jogo.

**Depois de uma atualização do kernel a correção parou de funcionar**
Rode `./install.sh` de novo para recompilar. Se o carregamento falhar, o volante
volta a funcionar como antes (pedais liga/desliga). Nada mais é afetado.

**A compilação falha em arquivos de outros dispositivos (ex.: `WALTOP`)**
O `install.sh` compila só o necessário e evita isso. O erro só aparece com um
`meson compile` completo e um clang < 19. Não afeta a correção do T80.

## Como funciona

### Diagnóstico

O T80 tem dois modos, escolhidos pela chave lateral:

- **PS4** (`044f:b668`): se apresenta com o descritor HID de um DualShock 4, incluindo os
  relatórios de autenticação do PS4 (`0xF0`–`0xF3`). Sem a autenticação, volante e
  pedais não enviam dados.
- **PS3** (`044f:b667`): descritor no estilo de um controle de PS3. O volante funciona,
  mas os pedais ficam nos bytes de "pressão analógica" dos botões L2/R2.

No modo PS3 o relatório de entrada tem 27 bytes, sem report ID:

| Bytes | Conteúdo | Declarado como |
|---|---|---|
| 0–1 | 13 botões + 3 bits de preenchimento | Button 1–13 |
| 2 | direcional (4 bits) + preenchimento | Hat switch |
| 3 | **volante** | Generic Desktop X |
| 4–6 | não usados (128) | Y, Z, Rz |
| 7–16 | pressão dos botões (não usados no T80) | Vendor `0xff00`, usages `0x20`–`0x29` |
| **17** | **pedal esquerdo (freio)** | **Vendor `0xff00`, usage `0x2a`** |
| **18** | **pedal direito (acelerador)** | **Vendor `0xff00`, usage `0x2b`** |
| 19–26 | 4 valores de 16 bits (sem movimento) | Vendor `0xff00`, usages `0x2c`–`0x2f` |

Os campos da página "vendor-defined" (`0xff00`) são ignorados pelo `hid-input` do
kernel. Por isso os pedais analógicos nunca chegam aos jogos.

### A correção

O programa em [`bpf/0010-Thrustmaster__T80.bpf.c`](bpf/0010-Thrustmaster__T80.bpf.c)
reescreve o descritor quando o kernel o lê, separando os dois bytes dos pedais e
declarando-os como eixos Generic Desktop **Rx** e **Ry**:

```
Original (31 bytes):                       Corrigido (40 bytes):
06 00 ff   Usage Page (Vendor 0xff00)      06 00 ff   Usage Page (Vendor 0xff00)
09 20..29  Usage 0x20..0x29                09 20..29  Usage 0x20..0x29
09 2a      Usage 0x2a                      95 0a      Report Count (10)
09 2b      Usage 0x2b                      81 02      Input (Data,Var,Abs)
95 0c      Report Count (12)               05 01      Usage Page (Generic Desktop)
81 02      Input (Data,Var,Abs)            09 33      Usage (Rx)    ← byte 17
                                           09 34      Usage (Ry)    ← byte 18
                                           95 02      Report Count (2)
                                           81 02      Input (Data,Var,Abs)
                                           06 00 ff   Usage Page (Vendor 0xff00)
```

- O resto do descritor fica idêntico, e o relatório continua com 27 bytes.
  **Os dados enviados pelo volante não mudam**, só a forma como o Linux os interpreta.
- O descritor passa de 137 para 146 bytes (é assim que o `check.sh` sabe se a correção está ativa).
- Antes de mexer, o programa confere o tamanho (137 bytes) e os bytes do trecho dos
  pedais. Com qualquer outro firmware ou descritor, ele não faz nada.
- Antes de carregar, o kernel verifica o programa BPF (sem acesso indevido à memória,
  sem loops infinitos). Ele não tem como travar o sistema.

## Referências

- [Oversteer, issue #181](https://github.com/berarma/oversteer/issues/181): relatos do T80
  no Linux (pedais só liga/desliga)
- [udev-hid-bpf](https://gitlab.freedesktop.org/libevdev/udev-hid-bpf) e
  [documentação](https://libevdev.pages.freedesktop.org/udev-hid-bpf/)
- [HID-BPF na documentação do kernel](https://docs.kernel.org/hid/hid-bpf.html)
- Exemplo semelhante no kernel: correção do Thrustmaster TCA Yoke Boeing
  (`drivers/hid/bpf/progs/Thrustmaster__TCA-Yoke-Boeing.bpf.c`)

## Licença

GPL-2.0-only (mesma licença das correções HID-BPF do kernel e do udev-hid-bpf).
