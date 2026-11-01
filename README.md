# FCEUmm Interface Edition — No Core

Esta é uma **versão de testes da interface para PlayStation 2**. Ela reaproveita a estrutura visual e os sistemas de navegação, USB, capas, leitura de ROMs, Turbo, gerenciamento de CHT e slots de teste derivados do PCE Fast.

**Nenhum núcleo de emulação está incluído, compilado, vinculado ou carregado.** Não há emulação de NES nesta edição.

## Estrutura exata do pendrive

Copie a pasta `FCEUmm` que está dentro de `USB-STRUCTURE` para a raiz do pendrive:

```text
PENDRIVE/
└── FCEUmm/
    ├── nes roms/
    ├── images/
    ├── cheats/
    └── STATES/
```

A pasta `nes roms` deve ficar exatamente em letras minúsculas. Os outros nomes também são sensíveis ao caminho utilizado pelo programa.

O ELF `FCEUmm-Interface-Only-PS2.ELF` pode ser colocado na raiz do pendrive ou em outro local de onde o usuário o inicie pelo uLaunchELF. O programa procura os dados em `mass:/FCEUmm/` e também tenta `mass0:/FCEUmm/`.

## O que esta versão testa

- Inicialização gráfica GSKit e background personalizado 640×480 incorporado no ELF.
- A imagem original fica em `assets/background.png` (1536×1152, proporção 4:3); o workflow a converte automaticamente para 640×480 no formato GS-native A1B5G5R5/PSMCT16.
- O background é embutido no ELF durante a compilação; não precisa ficar no pendrive.
- Inicialização do controle e navegação pelos menus.
- Carregamento dos mesmos módulos USB/exFAT usados na versão PCE Fast de referência.
- Navegação pela pasta `FCEUmm/nes roms/`.
- Listagem de arquivos `.nes` e `.zip`.
- Validação do cabeçalho iNES (`NES\x1A`) de ROMs `.nes`.
- Extração em memória de um membro `.nes` em ZIP padrão, sem criptografia, sem ZIP64 e com compressão Stored ou Deflate. O arquivo ZIP deve ser de disco único e respeitar o limite de 8 MiB desta versão.
- Carregamento de capas PNG 200×200 em `FCEUmm/images/`, usando o nome-base do jogo.
- Leitura de arquivos `.cht` NES em `FCEUmm/cheats/`, quando o nome-base do CHT corresponde ao nome-base da ROM. O leitor reconhece as chaves comuns `cheats`, `cheatN_desc`, `cheatN_code` e `cheatN_enable` do formato Libretro/RetroArch.
- Navegação no gerenciador de cheats e alternância visual ON/OFF.
- Teste de gravação, leitura e CRC dos cinco slots de teste em `FCEUmm/STATES/`.
- Inicialização do módulo audsrv para verificar a infraestrutura de áudio.

## Limites intencionais da edição sem core

| Recurso | O que realmente é testado |
|---|---|
| Cheats | Leitura do CHT, listagem e alternância visual. Os códigos **não são aplicados**. |
| Save States | Escrita, leitura, diretório e CRC. Não são estados reais de emulação. |
| ZIP | Leitura, extração e validação da ROM. O jogo não é executado. |
| Áudio | Carregamento/inicialização do audsrv. Não há áudio de jogo. |
| Turbo | Leitura dos botões e temporização visual. Não controla um jogo. |
| Vídeo | Interface e background. Não há quadros de emulação NES. |

Os arquivos de slot desta edição terminam em `.testslot1` até `.testslot5`, para não serem confundidos com futuros Save States reais do FCEUmm. Eles poderão ser apagados antes da integração do core.

## Drivers preservados

O pacote inclui os arquivos IRX usados como referência no PCE Fast:

- `usbd.irx`
- `usbhdfsd.irx`
- `audsrv.irx`

O workflow verifica os SHA-256 desses três arquivos antes de compilar. Isso confirma que os mesmos binários de driver foram empacotados; a leitura exFAT e a estabilidade ainda precisam ser testadas novamente com este ELF no PS2 real.

## Como gerar o ELF pelo GitHub Actions

1. Extraia todo o conteúdo deste pacote na raiz de um repositório GitHub.
2. Mantenha a branch principal com o nome `main`.
3. Envie os arquivos para o repositório. O workflow também pode ser iniciado manualmente em **Actions → FCEUmm Interface Edition - No Core → Run workflow**.
4. Aguarde a compilação terminar.
5. Baixe o artefato `FCEUmm-Interface-Edition-No-Core`.
6. Copie o ELF e a pasta `FCEUmm` para o pendrive.
7. Inicie o ELF no PS2 e teste os menus e os arquivos do pendrive.

O workflow compila somente o frontend. Ele não baixa nem compila QuickNES, Beetle PCE Fast, FCEUmm ou qualquer outro core.

## Próxima fase

Depois de aprovar a interface, USB, capas, CHT, controles e operações de teste dos slots no PS2, será preparada uma integração separada com o núcleo FCEUmm. Os slots `.testslot` não serão reaproveitados como estados reais.
