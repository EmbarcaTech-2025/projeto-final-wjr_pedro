# Projeto Final Embarcatech — TheraLink
**Alunos:** Wagner Junior e Pedro Henrique  
**Local:** EmbarcaTech — Brasília

> **Resumo:** Sistema **offline**, de **baixo custo**, que roda em uma **BitDogLab (RP2040 / Pico W)**, cria um **AP Wi-Fi** e expõe um **painel web** para a profissional acompanhar, em tempo real, **BPM** (oxímetro), **autoavaliação simples** (energia, humor, ansiedade) e **contagem por cores** (validação por sensor TCS34725). O fluxo no dispositivo usa **OLED + joystick** e dá **feedback imediato** (telas e LEDs).

---

## 1) Problema a ser resolvido
Em escolas e projetos sociais, muitas vezes **não há internet estável**. Psicólogos e educadores ainda assim precisam iniciar atividades com **turmas de ~20 pessoas**, obtendo rapidamente uma visão do **estado do grupo**. Sem uma ferramenta simples e **100% offline**, o atendimento começa “no escuro”, com **atrito operacional** e risco de decisões **pouco informadas**.

**TheraLink** oferece uma solução de instalação imediata e **sem internet** para coletar um **check-in** objetivo (**BPM**) e subjetivo (**autoavaliação** em três escalas), fornecendo ao profissional uma **visão consolidada** para orientar a sessão.

---

## 2) Como funciona na prática (simulação do uso)
1. **Liga o dispositivo**. Ele cria um **AP Wi-Fi** aberto chamado **`TheraLink`** (endereço local **`192.168.4.1`**).  
2. O participante posiciona o dedo no **oxímetro (MAX30102)**. O sistema mede **BPM** (e usa o valor final no registro).  
3. No **OLED** ou **Display Externo**, com o **joystick**, o participante informa **Energia**, **Humor** e **Ansiedade** (escala 1..3).  
4. O algoritmo `triage_decide(...)` calcula um **nível de risco** e **recomenda uma pulseira** (**verde/amarelo/vermelho**).  
5. Em seguida, o sensor **TCS34725** valida a **cor** da pulseira (lido no punho) por **razões R/G** estáveis.  
6. O sistema **salva o registro** em memória (RAM) e atualiza **médias/contagens**. As telas do **OLED** e a rota web **`/display`** mostram as mensagens em tempo real.  
7. A profissional se conecta ao AP **TheraLink** pelo celular/notebook e acessa **`http://192.168.4.1/`** para ver o **Painel** (gráfico de BPM, KPIs e contagem por cores). Também há **`/stats.json`** e **`/download.csv`**.

> **Observação:** os dados ficam **locais** (RAM). O **CSV** é um **agregado** para exportação rápida. Não há armazenamento em nuvem.

---

## Imagem da BitDogLab rodando o sistema

![alt text](etapa3\fotos\image.png)

## Imagem do Painel Web

![alt text](etapa3\telas\painel.png)

## 3) Requisitos Funcionais (RF)
- **RF01.** Acesso via **portal web local** (HTML/JS + JSON) hospedado pelo próprio dispositivo (**sem internet**).  
- **RF02.** Medição de **BPM** com **MAX30102** (suporte a MAX30100/30102).  
- **RF03.** **Autoavaliação** no **OLED** (joystick): **Energia**, **Humor**, **Ansiedade** (1..3).  
- **RF04.** **Recomendação de pulseira** via `triage_decide(...)` com faixas de BPM + escalas.  
- **RF05.** **Validação de cor** no braço com **TCS34725** (verde, amarelo, vermelho; tolerâncias por **razões R/G**).  
- **RF06.** **Consolidação em RAM**: média robusta de BPM, contagem por cor e médias de escalas.  
- **RF07.** **Feedback imediato**: telas no **OLED** (espelhadas em **`/display`**) e efeitos/LEDs.  
- **RF08.** **Painel do profissional** em **`/`** e **APIs**: **`/oled.json`**, **`/stats.json`** (com filtro `?color=`) e **`/download.csv`**.

---

## 4) Requisitos Não Funcionais (RNF)
- **RNF01.** Operação **100% offline**.  
- **RNF02.** Preparado para **ambientes sem Wi-Fi externo** (AP próprio).  
- **RNF03.** **Interação rápida**: ~**30 s por pessoa** (meta).  
- **RNF04.** Interface **inclusiva** com feedback **visual** (OLED/cores/LEDs).  
- **RNF05.** **Privacidade**: processamento local (sem Nuvem; exportação agregada em CSV).  
- **RNF06.** **Baixo custo** e montagem simples.

---

## 5) Hardware & Ligações (BitDogLab)
Periféricos utilizados:

- **OLED (SSD1306) — I²C1**  
  `SDA = GP14`, `SCL = GP15`, **ADDR = 0x3C`
- **Sensor de Cor (TCS34725) — I²C0**  
  `SDA = GP0`, `SCL = GP1`
- **Oxímetro (MAX3010x) — I²C0**  
  `SDA = GP0`, `SCL = GP1`
- **Botões**: `BUTTON_A = GP5`, `BUTTON_B = GP6`  
- **Joystick**: `X = ADC1/GP27`, `Y = ADC0/GP26`, **Botão = GP22`

> **SSID do AP:** `TheraLink` (aberto, sem senha, conforme `web_ap.c`).  
> **Gateway/host:** `192.168.4.1`.

## 6) Materiais (versão atual)
- **1× BitDogLab** (RP2040 Pico W com **OLED**, **matriz 5×5**, **joystick**, **Wi-Fi**)
- **1× Extensor I²C** (para levar o barramento I²C0 até os conectores J3/J4)
- **1× Oxímetro MAX30102** (I²C)
- **1× Sensor de cor TCS34725** (I²C)
- **Jumpers Dupont**, **fita dupla-face**, **power-bank** (ou fonte USB 5 V)

---

## 7) Ligações (hardware)
### 2.1. Barramento I²C da BitDogLab
- O **OLED** da BitDogLab usa **I²C1** (interno da placa).
- Os **sensores externos** (oxímetro + cor) usam **I²C0**.

### 2.2. Conexões com o extensor I²C (I²C0)
1. Conecte o **extensor I²C** à **entrada I²C0** da BitDogLab.  
   - **SDA (I²C0) → GP0**  
   - **SCL (I²C0) → GP1**  
   - VCC 3V3 e GND conforme o extensor.

2. No extensor, use os conectores:
   - **J3 → Oxímetro (MAX30102)**  
     - **SDA** (I²C)  
     - **SCL** (I²C)  
     - **3V3**  
     - **GND**
   - **J4 → Sensor de cor (TCS34725)**  
     - **SDA** (I²C)  
     - **SCL** (I²C)  
     - **3V3**  
     - **GND**

> O firmware já espera **I²C0 em GP0/GP1** para esses sensores.

---

## 8) Passo a passo de montagem (rápido)
1. **Fixe** o extensor I²C na BitDogLab e leve SDA/SCL/3V3/GND até ele.  
2. **Conecte o MAX30102 no J3** do extensor (atenção a VCC e GND).  
3. **Conecte o TCS34725 no J4** do extensor (atenção a VCC e GND).  
4. **Conferir**: nada invertido; cabos firmes; sem curto.  
5. Alimente a BitDogLab com **USB** (PC) ou **power-bank**.

---

## 9) Instalação & Build (compilação do firmware)

1. Configure o **Pico SDK** (com **cyw43** e **lwIP** habilitados para o Pico W).  
2. No diretório do projeto, gere a build (ex.: **CMake + Ninja**):
   ```bash
   mkdir build && cd build
   cmake .. -DPICO_BOARD=pico_w
   ninja
3. Foi utilizado VSCode no desenvolvimento do projeto, recomendado caso use Windows.
4. Nesse vídeo, tem um tutorial de como rodar projetos com a BitDogLab no VSCode: https://www.youtube.com/watch?v=uVK-OHy2XZg