# App nativo Windows — HF Studio

> Status: **ponderando** — nada implementado ainda. Registrado em 2026-11-04.
> Origem: sessão do `hf-local-studio` (app web Python/FastAPI rodando no WSL).

## Motivação

- O app web atual funciona, mas acumulamos problemas de **runtime**: torch `+cpu` vs `+xpu`,
  dependências SYCL/MKL, limitações do WSL (NPU inacessível, GPU via DirectX).
- Um app **nativo Windows** remove camadas: sem WSL, sem Python, sem .NET.
- Hardware alvo: **Intel Core Ultra (Lunar Lake)** — GPU Arc 140V + **NPU AI Boost**.
  A NPU só é acessível nativamente no Windows (limitação do WSL2).

## Decisão de arquitetura

- **C++ de ponta a ponta** (C++/WinRT), interface **WinUI 3** (Windows App SDK).
- Obs.: o Windows Terminal **não** é precedente de WinUI 3 — ele usa WinUI 2 via
  XAML Islands. A documentação e os templates da Microsoft priorizam C#; C++/WinRT
  é suportado, mas com menos exemplos.
- Empacotamento: **MSIX**, com o redistribuível do OpenVINO embutido.

## Stack

| Camada | Escolha |
|---|---|
| UI | WinUI 3 (XAML) via C++/WinRT |
| Inferência | **OpenVINO GenAI** (C++) |
| Texto | `ov::genai::LLMPipeline` → device `"NPU"` |
| Imagem | `ov::genai::Text2ImagePipeline` → device `"GPU"` |
| Modelos | Org **`OpenVINO`** no Hugging Face — coleção *"LLMs optimized for NPU"* (modelos `*-int4-cw-ov`) |
| Download | Cliente HF Hub via REST (`/api/models`, `/resolve/main/...`, `Range` p/ resume) |

## Cenário ideal

Texto na **NPU** + imagem na **GPU** ao mesmo tempo, com a NPU rodando o LLM com
consumo baixo de energia.

## Ressalvas

- NPU: contexto **padrão** é só ~1.150 tokens (`MAX_PROMPT_LEN`=1024 +
  `MIN_RESPONSE_LEN`=128). Dá pra aumentar passando essas propriedades ao criar o
  pipeline — `max_length` sozinho não muda nada. Histórico de chat consome esse limite.
- NPU exige pesos **int4 simétricos** (de preferência channel-wise, `--sym --group-size -1`);
  modelo int4 assimétrico não roda → manter lista curada de modelos testados.
- Primeiro token tende a ser mais lento que na GPU.
- Não existe SDK C++ do HF Hub — implementar via REST.
- WinUI 3 em C++/WinRT é mais verboso que C# (custo aceito).

## Roadmap

1. **Spike**: app console C++ que baixa um modelo int4 da org OpenVINO e gera texto na
   NPU — valida performance e download antes da UI.
2. Core: `ModelManager` (busca/download/cache) + engines de texto e imagem.
3. UI WinUI 3: busca, downloads, modelos locais, chat, geração de imagem.
4. Empacotamento MSIX.
5. **(Depois)** Conversão de modelos do HF no download — ver seção abaixo.

## Core (passo 2 do roadmap) — concluído

`HubClient`, `ModelManager`, `TextEngine` e `ImageEngine` em `core/`, mais a CLI
`hfstudio.exe`. Ver `README.md`.

**Cenário dos sonhos validado:** texto na NPU (16,5 tok/s) e imagem na GPU (1,5 s por imagem
512² com LCM) rodando ao mesmo tempo. Sozinhos: 22,5 tok/s e 0,8 s — a perda vem da RAM
compartilhada, mas os dois seguem usáveis em paralelo.

## UI WinUI 3 (passo 3 do roadmap) — primeira versão funcionando

App `NpuStudio` em `app/` (C++/WinRT, Windows App SDK 2.6.5, não empacotado e self-contained).
Telas Chat, Imagem e Modelos usando o core. Testado pela interface: download do
Qwen3-0.6B (434 MB em 36 s), chat na NPU (Phi-3.5, 23 tok/s) e na GPU (Qwen3-0.6B, 89 tok/s),
imagem na GPU (LCM, 0,9 s). Pendências no `README.md`.

Próximo: polimento (Markdown no chat, salvar conversas, configurações) e depois passo 4 — MSIX.

## Ideia futura: converter qualquer modelo do HF no download

Não existe conversor em C++; o oficial é o `optimum-intel` (Python).

> **Decidido (2026-11-04):** Python **somente para converter modelos**; todo o resto do
> sistema (UI, download, inferência, cache) continua em C++. O app nunca depende de
> Python para rodar — sem o conversor instalado, só modelos já convertidos ficam disponíveis.

Como fica:

- **Conversor embutido e isolado**: Python próprio instalado sob demanda na pasta de dados
  do app (não mexe no Python do sistema), chamado como processo separado só para converter.
  O app continua C++.
- Comando: `optimum-cli export openvino --weight-format int4 --sym --group-size -1 --ratio 1.0`
  — esse formato roda na NPU **e** na GPU, então uma conversão serve para os dois.
- Fluxo: pedido `Qwen/Qwen3-4B` → procura `OpenVINO/<nome>-int4-cw-ov` pronto → se não
  houver, baixa o original e converte → pré-compila cache para NPU e GPU.
- Limites: RAM (com 32 GB, até ~4B com folga, 7–8B no limite), minutos de conversão,
  nem toda arquitetura é suportada, ~3 GB para o ambiente, modelos restritos pedem token HF.
- Alternativa sem Python: aceitar só modelos já convertidos (org OpenVINO + comunidade).
- Validar primeiro como `spike convert <repo>` antes de levar para o app.

## Setup do Windows

1. **Drivers Intel**: driver gráfico Arc + driver da NPU (AI Boost). Verificação:
   `Get-CimInstance Win32_PnPEntity | Where-Object Name -match "AI Boost|Arc"`
   (Checado em 2026-11-04: ambos OK. Driver da GPU é de ago/2025 — atualizar.)
2. **Toolchain**: **Visual Studio 2026** com o workload **"WinUI application
   development"** + o item **"C++ WinUI app development tools"** (para C++),
   **"Desktop development with C++"** (traz CMake e MSVC), e **Developer Mode**
   ligado no Windows. Atalho oficial: `winget configure -f https://aka.ms/winui-config`
   (instala o workload base e liga o Developer Mode; o item C++ é marcado à parte).
   + Git for Windows.
3. **OpenVINO GenAI** (zip para Windows, roda `setupvars.bat` em cada terminal novo).
   Download oficial: `https://storage.openvinotoolkit.org/repositories/openvino_genai/packages/<versão>/windows/`
   (o antigo `storage.openvino.ai` não existe mais).

## Estado da máquina (2026-11-04)

- VS Community 2026 (18.11) — MSVC 14.51, CMake, WinUI C++, Windows SDK 10.0.26100
- Git 2.56
- OpenVINO GenAI **2026.4.1** em `C:\Intel\openvino_genai_windows_2026.4.1.0_x86_64`,
  com junction estável `C:\Intel\openvino_genai` (usar este caminho nos builds)
- Dispositivos vistos pelo OpenVINO: CPU, `GPU` (Arc 140V), `NPU` (AI Boost,
  compilador NPU 8.4 presente via driver ✅)
- Driver GPU atualizado para 32.0.101.9150 (ago/2026) ✅
- Pendente: Developer Mode (necessário para rodar app WinUI)

## Spike (passo 1 do roadmap) — feito em 2026-11-04

Ver `spike/README.md`. Resumo: download REST do HF com resume + SHA-256 funcionando;
Phi-3.5-mini int4-cw na **NPU a ~22 tok/s** (carga 2,1 s com cache, 39 s sem) e na
**GPU a ~34 tok/s** (carga 1,0 s com cache). A GPU só funcionou após atualizar o driver
para 32.0.101.9150 — com o driver de 2025 ela gerava 0,07 tok/s.
4. Opcional: Python 3.11+ só se for converter modelos com `optimum-intel`.
