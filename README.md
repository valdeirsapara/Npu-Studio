# NPU Studio

App nativo Windows (C++ / WinUI 3) para baixar modelos do Hugging Face e rodá-los localmente
na NPU / GPU Intel via OpenVINO GenAI. Plano e decisões em [docs/PLANO.md](docs/PLANO.md).

## Estrutura

| Pasta | O que é |
|---|---|
| `app/` | **NPU Studio** — app WinUI 3 em C++/WinRT (Chat, Imagem, Modelos) |
| `core/` | Biblioteca C++ (`hfstudio_core`) — a lógica do app, sem interface |
| `cli/` | `hfstudio.exe`, front-end de terminal para testar o core |
| `spike/` | Protótipo inicial (mantido como referência; build próprio) |
| `third_party/` | `nlohmann/json` 3.11.3 (MIT), copiado do pacote OpenVINO GenAI |

### Core

- `http.hpp` — WinHTTP: GET de texto, download com retomada (`Range`), SHA-256 (BCrypt).
- `hub_client.hpp` — API REST do HF: busca de modelos OpenVINO, commit da versão, lista de arquivos.
- `model_manager.hpp` — biblioteca local em `%LOCALAPPDATA%\HFStudio\models\`:
  - cada modelo em `<dono>--<nome>/` com manifesto `hfstudio.json`;
  - download vai para `.downloads/` e só entra na biblioteca completo e verificado;
  - versão fixada no commit (todos os arquivos da mesma versão);
  - detecta compatibilidade com a NPU lendo a quantização no IR (`int4_sym` + `group_size=-1`).
- `text_engine.hpp` — chat multi-turno com `LLMPipeline` + `ChatHistory`; streaming com
  interrupção; cache de compilação por dispositivo (`<modelo>/.ov_cache/<device>`); descarta as
  trocas mais antigas quando o histórico passa do limite de contexto.
- `image_engine.hpp` — texto→imagem com `Text2ImagePipeline` (GPU por padrão); progresso por
  passo com cancelamento; seed aleatória reportada (ou fixa, para repetir); passos padrão por
  modelo (4 para LCM/turbo/schnell, 25 para os demais); `save_png` via WIC (nativo do Windows).

Todas as operações longas aceitam callback de progresso/cancelamento — a UI vai rodá-las em
threads de fundo.

## App (WinUI 3)

```bat
build_app.bat
build\app\Release\NpuStudio.exe
```

`build_app.bat` baixa o `nuget.exe` (se faltar), restaura os pacotes do Windows App SDK
2.6 em `app\packages`, compila o core (CMake) e o app (MSBuild). O app é **não empacotado e
self-contained**: o `.exe` roda direto, sem instalar o Windows App Runtime, e as DLLs do
OpenVINO são copiadas para a pasta do `.exe`. Só existe a configuração **Release|x64**
(o OpenVINO só distribui bibliotecas Release).

Telas:

- **Chat** — escolhe modelo e dispositivo (NPU para modelos compatíveis, senão GPU), carrega,
  conversa com streaming; Enter envia, Shift+Enter quebra linha; botão Parar; "Nova conversa";
  esconde blocos `<think>` (Qwen3); rodapé com tok/s, 1º token e uso de contexto.
- **Imagem** — modelo, dispositivo, prompt, prompt negativo, tamanho, passos, seed; progresso
  por passo e Cancelar; salva PNG em `Imagens\NPU Studio`; histórico de miniaturas da sessão.
- **Modelos** — Explorar (busca no HF, selo NPU, download com progresso, Pausar/Continuar) e
  Instalados (tamanho, quantização, Remover).

Barra de título própria (`TitleBar`) com o botão de menu ao lado do ícone e os dispositivos
detectados; fundo Mica.

## CLI e core

Requisitos: VS 2026 (C++), OpenVINO GenAI em `C:\Intel\openvino_genai`.

```bat
build.bat
hfstudio.bat search qwen3 --author OpenVINO
hfstudio.bat download OpenVINO/Phi-3.5-mini-instruct-int4-cw-ov
hfstudio.bat list
hfstudio.bat chat OpenVINO/Phi-3.5-mini-instruct-int4-cw-ov --device NPU
hfstudio.bat remove OpenVINO/Phi-3.5-mini-instruct-int4-cw-ov
hfstudio.bat download OpenVINO/LCM_Dreamshaper_v7-int8-ov
hfstudio.bat image OpenVINO/LCM_Dreamshaper_v7-int8-ov "a cozy cabin in the snow" --out out\cabana.png
```

`image` aceita `--size 768x512`, `--steps`, `--seed`, `--guidance`, `--negative`, `--count`.
Sem `--out`, salva `imagem_<seed>.png` na pasta atual.

No chat: `/reset`, `/stats`, `/sair`; Ctrl+C interrompe a resposta. Em download, Ctrl+C pausa e
o mesmo comando continua de onde parou. Modelos restritos: defina `HF_TOKEN`.

## Testado (2026-11-04)

- Busca, download, retomada no meio de um arquivo de 600 MB, verificação SHA-256, remoção.
- Detecção NPU: Phi-3.5-mini `int4-cw` → sim; LFM2.5 `int4-gq` (group_size=128) → não.
- Chat multi-turno: NPU ~23 tok/s, GPU ~31 tok/s; corte de histórico com contexto pequeno.
- Imagem (LCM Dreamshaper v7 int8, 512×512, 4 passos, GPU): carga 23 s na 1ª vez / 1,6 s com
  cache; **0,8 s por imagem** depois da primeira.

### Texto na NPU + imagem na GPU ao mesmo tempo ✅

Dois processos: chat de 400 tokens na NPU enquanto a GPU gerava 15 imagens.

| | Sozinho | Simultâneo |
|---|---|---|
| Texto (NPU, Phi-3.5-mini) | 22,5 tok/s | 16,5 tok/s (−27%) |
| Imagem (GPU, LCM 512²) | 0,8 s/img | 1,5 s/img |

Funciona sem erros e as imagens saem corretas. A queda vem da memória compartilhada (NPU e
GPU usam a mesma RAM): ambos continuam bem usáveis, mas não é "de graça".

## Pendências conhecidas

- Chat não renderiza Markdown (negrito, listas, código aparecem como texto puro).
- Conversas não são salvas ao fechar o app.
- Sem tela de configurações (pasta dos modelos, token do HF, tamanho do contexto).
- Empacotamento MSIX (passo 4 do roadmap).

- Mudar `--ctx`/`--max-new` na NPU recompila o modelo (~1 min) na primeira vez para cada combinação.
- Phi-3.5-mini responde mal em português; testar modelos melhores (ex.: Qwen3 `int4-cw`).
- Busca não indica tamanho do modelo (exige uma chamada extra por resultado).
