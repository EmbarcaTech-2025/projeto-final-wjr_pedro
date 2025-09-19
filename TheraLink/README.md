## Arquitetura de Software (módulos)
- **`main.c`** — Máquina de estados da triagem (`state_t`), telas do OLED, integração dos sensores e estatísticas.  
- **`src/oximetro.c/.h`** — Driver e **estado** do MAX3010x; entrega **BPM ao vivo** e **BPM final**.  
- **`src/cor.c/.h`** — Driver **TCS34725** (init, leitura bruta e normalizada) e **classificação por razão** (verde/amarelo/vermelho, branco/preto).  
- **`src/stats.c/.h`** — Acumula métricas (média robusta de BPM, contagem por cor, médias de ansiedade/energia/humor) e gera **CSV**.  
- **`src/ssd1306_i2c.c/.h` + `ssd1306.h`** — Driver do **OLED** (draw string, clear, show).  
- **`src/web_ap.c/.h`** — **AP Wi-Fi + DHCP + DNS + HTTP (lwIP)**, páginas **`/`** e **`/display`**, e APIs JSON/CSV.

### Estados principais (`main.c`)
`ST_ASK → ST_OXI_PREP → ST_OXI_RUN → ST_SHOW_BPM → ST_ENERGY_ASK → ST_HUMOR_ASK → ST_ANS_ASK → ST_TRIAGE_RESULT → ST_COLOR_INTRO → ST_COLOR_LOOP → ST_SAVE_AND_DONE → ST_REPORT`

---

## Endpoints HTTP (servidor local)
- **`GET /`** — **Painel do profissional**:  
  Gráfico de BPM (histórico curto), KPIs (**Energia**, **Humor**, **Ansiedade**), **barras por cor** e filtro por **grupo**.
- **`GET /display`** — Espelha as **4 linhas** atuais do **OLED** (com destaque de palavras “verde/amarelo/vermelho”).  
- **`GET /oled.json`** — `{ "l1": "...", "l2": "...", "l3": "...", "l4": "..." }`  
- **`GET /stats.json`** — Resumo **agregado**. Suporta `?color=verde|amarelo|vermelho`.  
  **Exemplo de resposta:**
  ```json
  {
    "bpm_live": 0.0,
    "bpm_mean": 78.2,
    "bpm_n": 12,
    "cores": { "verde": 7, "amarelo": 3, "vermelho": 2 },
    "ans_mean": 2.0,   "ans_n": 12,
    "energy_mean": 2.2, "energy_n": 12,
    "humor_mean": 2.4,  "humor_n": 12
  }