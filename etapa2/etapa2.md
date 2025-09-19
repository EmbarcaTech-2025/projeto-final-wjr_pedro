# TheraLink — Etapa 2 (Arquitetura e Modelagem)

**Alunos:** Wagner Junior e Pedro Henrique  
**Plataforma:** BitDogLab (RP2040 Pico W)

> Sistema offline, de baixo custo, que roda inteiramente na BitDogLab: a placa cria um **AP Wi-Fi**, executa **DHCP/DNS/HTTP**, lê **MAX30102** (BPM) e **TCS34725** (cor), coleta **autoavaliacoes** (energia, humor, ansiedade) no **OLED + joystick**, calcula **risco** e registra estatisticas em **RAM**. A visualizacao ocorre no **OLED** e no **painel web** local.

---

## 1. Arquitetura e Modelagem

- **No unico (Pico W/BitDogLab)** operando como **Access Point** e **servidor HTTP** (lwIP): nao depende de internet externa.  
- **Camadas (visao 6-camadas IoT):**
  1) **Sensor**: MAX30102 (BPM), TCS34725 (cor), botoes e joystick  
  2) **Conectividade**: Wi-Fi AP do Pico W  
  3) **Borda/Processamento**: filtragem de batimentos, deteccao de picos, `triage_decide` (BPM + escalas -> cor) e classificacao de cor (razoes R/G)  
  4) **Armazenamento**: estatisticas **em RAM** (media robusta de BPM, contagens por cor, medias das escalas)  
  5) **Abstracao/Servicos**: endpoints HTTP (`/`, `/display`, `/oled.json`, `/stats.json`, `/download.csv`)  
  6) **Exibicao**: OLED local e **painel web** responsivo  
- **Privacidade e operacao**: processamento local, sem nuvem; exportacao rapida via **CSV** agregado.

---

## 2. Diagramas

### 2.1 Diagrama de Hardware

![Diagrama de Hardware](./images/hardware_diagram.png)

**Explicacao**  
O diagrama organiza o sistema em **quatro blocos**: **Rede Wi-Fi**, **Placa BitDog (Pico W)**, **Sensores Externos (I2C0)** e **Alimentacao**.  
- **Rede Wi-Fi:** a BitDog atua como **Access Point** e hospeda **AP/DHCP/DNS/HTTP**; o profissional conecta o celular/notebook diretamente ao AP e acessa as paginas locais via **HTTP GET** (operacao **offline** e baixa latencia).  
- **Placa BitDog (Pico W):** o **RP2040** centraliza a logica e conversa com os perifericos on-board: **OLED SSD1306 em I2C1 (SDA GP14, SCL GP15)**, **matriz WS2812 5x5 (DIN GP7)**, **LEDs RGB R/G/B em GP13/GP12/GP11**, **botoes A/B em GP5/GP6**, **joystick SW em GP22 e eixos ADC VRx/VRy em GP27/GP26**, e **buzzers A/B em GP21/GP10**.  
- **Sensores Externos (I2C0):** o **I2C0 (SDA GP0, SCL GP1)** sai por um **extensor** ate os modulos: **TCS34725 (0x29)** e **MAX30102 (0x57)**. Ambos compartilham o barramento (tipicamente 100 kHz; suporta ate 400 kHz). **GND comum** e **pull-ups** presentes no conjunto.  
- **Alimentacao:** duas opcoes usuais — **USB 5 V -> 3V3** (melhor estabilidade dos sensores) ou **bateria 18650 3,7 V -> 3V3** (mobilidade). Toda a logica e **3,3 V**; a **WS2812** recebe **5 V** com **dados em 3,3 V** (compatibilidade OK na BitDog).

### 2.2 Diagrama de Blocos

![Diagrama de Blocos](./images/blocos.png)

**Explicacao**  
O diagrama mostra o fluxo entre os modulos:  
1) **Gerenciador de Sensores** integra **MAX30102** (HR/SpO2) e **TCS34725** (leitura RGB/clear).  
2) **Validacao e Processamento** verifica presenca do dedo e qualidade do sinal, calcula **BPM final**, classifica a **cor**, e registra as escalas de **energia/humor/ansiedade** recebidas pelo **joystick**.  
3) **Agregador e Estatisticas** mantem **contagens/medias** em RAM e gera um **snapshot** para exibicao.  
4) **Feedback local**: telas no **OLED**, matriz/LEDs e buzzer para retorno imediato ao participante.  
5) **Servidor HTTP** expõe **`/`**, **`/display`**, **`/oled.json`**, **`/stats.json`** e **`/download.csv`**.  
6) **Portal Web** (HTML/JS) acessado pelo AP exibe, **em tempo real**, os resultados processados — tudo **offline**.

---

## 3. Fluxograma de Software

![Fluxograma de Software](./images/fluxogram.png)

**Resumo do fluxo (conforme o codigo):**
1) **Oxi prep** -> mede BPM final  
2) **Escalas** no OLED (energia, humor, ansiedade)  
3) **Triagem** -> cor recomendada (verde/amarelo/vermelho)  
4) **Validacao de cor** com TCS34725 (normalizacao + razao)  
5) **Salvar** em stats (RAM), **atualizar** OLED e **painel web**  
6) **Relatorio rapido** no OLED -> **repetir** ou **finalizar**

---

## 4. Endpoints HTTP e Painel

- **GET /** — **Painel** do profissional: grafico curto de BPM, KPIs (medias de energia/humor/ansiedade), barras por cor e filtro por cor/grupo.  
- **GET /display** — espelho das **4 linhas** atuais do OLED (para visual remoto).  
- **GET /oled.json** — `{"l1":"...","l2":"...","l3":"...","l4":"..."}`  
- **GET /stats.json** — resumo agregado; aceita `?color=verde|amarelo|vermelho`.  
  **Exemplo:**
  ```json
  {
    "bpm_live": 0.0,
    "bpm_mean": 78.2,
    "bpm_n": 12,
    "cores": {"verde":7,"amarelo":3,"vermelho":2},
    "ans_mean": 2.0, "ans_n": 12,
    "energy_mean": 2.2, "energy_n": 12,
    "humor_mean": 2.4, "humor_n": 12
  }
