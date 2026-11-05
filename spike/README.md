# Spike — download do HF + texto na NPU

App console C++ que valida as duas peças de risco do plano antes da UI:

1. **Download do Hugging Face via REST** (`src/hf_download.cpp`, WinHTTP, sem dependências):
   lista arquivos por `/api/models/{repo}/tree/main?recursive=true`, baixa por
   `/{repo}/resolve/main/{arquivo}` (redirect para a CDN), resume com `Range` a partir de
   `*.part`, pula arquivos já completos e confere SHA-256 dos arquivos LFS (BCrypt).
2. **Geração de texto** com `ov::genai::LLMPipeline` em NPU/GPU/CPU (`src/main.cpp`),
   com `CACHE_DIR` para reaproveitar o modelo compilado e métricas de desempenho.

## Uso

```bat
spike\build.bat
spike\spike.bat download OpenVINO/Phi-3.5-mini-instruct-int4-cw-ov models\Phi-3.5-mini-instruct-int4-cw-ov
spike\spike.bat run models\Phi-3.5-mini-instruct-int4-cw-ov NPU "seu prompt"
```

`spike.bat` chama o `setupvars.bat` do OpenVINO antes de rodar (as DLLs precisam estar no PATH).

## Resultados (2026-11-04)

Máquina: Core Ultra 7 258V, 32 GB, NPU driver 32.0.100.4841, GPU driver 32.0.101.7026 (ago/2025).
OpenVINO GenAI 2026.4.1. Modelo: `OpenVINO/Phi-3.5-mini-instruct-int4-cw-ov` (1,9 GB).
Prompt de 25 tokens, 300 tokens gerados, `MAX_PROMPT_LEN=2048`, `MIN_RESPONSE_LEN=512`.

### Rodada 1 — driver GPU antigo (32.0.101.7026, ago/2025)

| | NPU (1ª carga) | NPU (com cache) | GPU |
|---|---|---|---|
| Carga | 38,8 s | 2,8 s | 11,1 s |
| TTFT | 1.436 ms | 1.291 ms | 158 ms |
| Tempo por token | 58,7 ms | 57,6 ms | ⚠️ 13.737 ms |
| Throughput | 17,1 tok/s | 17,4 tok/s | ⚠️ 0,07 tok/s |

A GPU gerava ~13,7 s por token — defeito, não limite de hardware.

### Rodada 2 — driver GPU atualizado (32.0.101.9150, ago/2026) ✅

| | NPU (com cache) | GPU (1ª carga) | GPU (com cache) |
|---|---|---|---|
| Carga | 2,1 s | 7,7 s | **1,0 s** |
| TTFT | 1.122 ms | 117 ms | **106 ms** |
| Tempo por token | 45,2 ms | 33,8 ms | 29,6 ms |
| Throughput | **22,1 tok/s** | 29,6 tok/s | **33,8 tok/s** |

**Causa confirmada:** driver antigo da GPU. Com o driver novo a GPU ficou ~480× mais
rápida, e a NPU também melhorou (17 → 22 tok/s), provavelmente pelo reboot/atualização.

Download: 1,9 GB a ~20 MB/s, 4/4 arquivos LFS com SHA-256 ok.

## Conclusões

- NPU e GPU funcionam de ponta a ponta em C++ puro, sem Python.
- **GPU**: ~1,5× mais rápida na geração e ~10× no primeiro token (106 ms vs 1,1 s).
  **NPU**: 22 tok/s, confortável para chat, e libera a GPU (ex.: para gerar imagem ao
  mesmo tempo) com menor consumo. Ambas são boas opções — o app deve deixar escolher.
- **Driver da GPU importa muito**: o app deve checar a versão e avisar se estiver antiga.
- **Cache é obrigatório**: sem ele cada abertura do app custa ~40 s de compilação.
  No app, pré-compilar logo após o download, com progresso na UI.
- TTFT de ~1,3 s para 25 tokens de prompt é perceptível; medir com prompts longos
  (histórico de chat) antes de decidir o orçamento de contexto.
- O parser JSON por regex basta para o spike; no app usar uma lib JSON de verdade
  (ex.: `Windows.Data.Json` via C++/WinRT, que já vem no Windows).
