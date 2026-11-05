# HF Studio

App nativo Windows (C++) para baixar modelos do Hugging Face e rodá-los localmente na
NPU / GPU Intel via OpenVINO GenAI. Plano e decisões em [docs/PLANO.md](docs/PLANO.md).

## Estrutura

| Pasta | O que é |
|---|---|
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

Todas as operações longas aceitam callback de progresso/cancelamento — a UI vai rodá-las em
threads de fundo.

## Build e uso

Requisitos: VS 2026 (C++), OpenVINO GenAI em `C:\Intel\openvino_genai`.

```bat
build.bat
hfstudio.bat search qwen3 --author OpenVINO
hfstudio.bat download OpenVINO/Phi-3.5-mini-instruct-int4-cw-ov
hfstudio.bat list
hfstudio.bat chat OpenVINO/Phi-3.5-mini-instruct-int4-cw-ov --device NPU
hfstudio.bat remove OpenVINO/Phi-3.5-mini-instruct-int4-cw-ov
```

No chat: `/reset`, `/stats`, `/sair`; Ctrl+C interrompe a resposta. Em download, Ctrl+C pausa e
o mesmo comando continua de onde parou. Modelos restritos: defina `HF_TOKEN`.

## Testado (2026-11-04)

- Busca, download, retomada no meio de um arquivo de 600 MB, verificação SHA-256, remoção.
- Detecção NPU: Phi-3.5-mini `int4-cw` → sim; LFM2.5 `int4-gq` (group_size=128) → não.
- Chat multi-turno: NPU ~23 tok/s, GPU ~31 tok/s; corte de histórico com contexto pequeno.

## Pendências conhecidas

- **ImageEngine** (`Text2ImagePipeline` na GPU) — próximo passo.
- Mudar `--ctx`/`--max-new` na NPU recompila o modelo (~1 min) na primeira vez para cada combinação.
- Phi-3.5-mini responde mal em português; testar modelos melhores (ex.: Qwen3 `int4-cw`).
- Busca não indica tamanho do modelo (exige uma chamada extra por resultado).
