# TheraLink — Etapa 4

**Alunos:** Wagner Junior e Pedro Henrique  
**Plataforma:** BitDogLab (RP2040 Pico W)

# TheraLink — Visão geral e motivação

### O que é o projeto

O **TheraLink** é um sistema que você liga e começa a usar **sem internet**. Ela cria uma **rede Wi-Fi própria**; você conecta o **celular** ou **notebook** nessa rede e abre um **painel** que mostra, em tempo real, como o grupo está chegando para a atividade.  
Cada participante faz um **check-in rápido**: coloca o dedo no **sensor de batimentos** para medir **BPM** e responde, na telinha com joystick, três perguntas simples sobre **energia**, **humor** e **ansiedade**. Com isso, o sistema sugere uma **cor de prioridade** (**verde**, **amarelo** ou **vermelho**) e **confirma** a cor com um **sensor de leitura de cores**.  
Tudo acontece **localmente**, dentro do aparelho: nada vai para a nuvem, os dados ficam **anônimos** e você ainda pode **baixar um resumo** em CSV no final para registrar como estava o clima do grupo.

> **Explicação técnica:** a placa é uma **BitDogLab (Pico W)**. Ela sobe um **ponto de acesso Wi-Fi**, roda **DHCP/DNS/HTTP**, lê o **MAX30102** (sensor batimentos) e o **TCS34725** (sensor cor), mostra mensagens no **OLED** e recebe respostas pelo **joystick**. A **prioridade** é calculada combinando BPM + respostas; as **estatísticas** ficam em memória e aparecem no painel web local.

---

## 1) Problema e motivação

### Por que isso existe?
Em atividades com **turmas grandes** (escolas, projetos sociais, acolhimento) que passaram por um evento traumático recente, quem conduz a sessão precisa de um **sinal rápido** de como o grupo está. Sem essa leitura inicial, a condução começa **no escuro**: a dinâmica pode ser calma demais para um grupo elétrico ou intensa demais para um grupo ansioso — o que **piora a experiência** de todo mundo.

Além disso, **conexão instável** é realidade em muitos lugares. Relatórios **UNICEF/ITU** indicam que **cerca de dois terços** das crianças e jovens **não têm internet em casa** [1]; mesmo em escolas, o Wi-Fi pode não dar conta. Ou seja, **soluções offline** e **fáceis de operar** fazem diferença de verdade na ponta.

### O que o TheraLink entrega de útil
O TheraLink foi feito para **reduzir atrito** e **dar contexto** em **~30 segundos** por pessoa:

- **Um dado objetivo:** **BPM** pelo oxímetro — não serve para diagnóstico médico, mas ajuda a indicar **tensão** ou **agitação** (baseado em achados de HRV/arousal) [4][5].  
- **Três sinais subjetivos:** **energia**, **humor** e **ansiedade**, em escala simples (1–3), fáceis de responder no **OLED + joystick** (inspirado em materiais de educação socioemocional) [2][3].  
- **Uma síntese visual:** **verde / amarelo / vermelho** como **prioridade**, inspirada em esquemas de **triagem** amplamente entendidos pela população [6][7]. A cor é **validada** por um **sensor de cor**, o que torna a experiência **lúdica** e **inclusiva** (especialmente com crianças).  
- **Visão do grupo em tempo real:** no **painel web local**, o profissional vê **médias**, **contagens por cor** e **gráfico** de BPM, sem depender de internet.

### De onde veio a ideia (inspirações)
- **Organizar emoções em eixos simples** (energia/valência) vem de materiais de **SEL** como o **RULER/Mood Meter** do Yale, ajudando a transformar sentimentos em **informação prática** [2][3].  
- **Usar batimentos/HRV** como **proxy de arousal/estresse** é comum em pesquisas (útil como **contexto**, não diagnóstico) [4][5].  
- **Prioridade por cores** no estilo **semáforo** (verde/amarelo/vermelho) é um padrão de **comunicação instantânea** inspirado em **triagem** (ex.: START) [6][7].

### O ganho na prática
Com o TheraLink, a sessão começa **mais informada**: em poucos minutos, você tem uma **fotografia do grupo** (ex.: “metade verde, 30% amarelo, 20% vermelho; BPM médio 78; ansiedade média 2,0”). Isso ajuda a **escolher dinâmicas**, **priorizar atenção** e **acompanhar a evolução** ao longo do encontro — tudo **sem internet**, com **baixo custo** e **operação simples**.

---

## 2) Como o sistema funciona (visão prática)

1. **Liga o dispositivo.** A BitDogLab sobe um **AP Wi-Fi local** (SSID `TheraLink`, IP padrão `192.168.4.1`) e inicializa os periféricos.  
2. **Medição fisiológica.** O participante posiciona o dedo no **MAX30102**; o firmware coleta batimentos, filtra picos e calcula o **BPM final**.  
3. **Autoavaliação.** No **OLED**, com o **joystick**, o participante informa **Energia**, **Humor** e **Ansiedade** (escala 1–3).  
4. **Triagem.** A função `triage_decide` combina **BPM** e as **três escalas** e recomenda **uma cor** (verde/amarela/vermelha).  
5. **Validação de cor.** O participante aproxima a pulseira do **TCS34725**; o sistema **normaliza** a leitura e **classifica** a cor por **razões entre canais** (mais estáveis que comparar valores “crus”).  
6. **Registro e visualização.** As métricas são **acumuladas em RAM** (média robusta de BPM, contagem por cor, médias das escalas). O **OLED** mostra confirmações/relatórios, e o **painel web** é atualizado em tempo real via endpoints HTTP locais.  
7. **Repetição.** O fluxo retorna ao início para o próximo participante.

> **Dados locais:** não há nuvem; exportação rápida via **CSV agregado**.

---

## 3) Requisitos do sistema

### 3.1 Requisitos Funcionais (RF)
- **RF01.** Disponibilizar **portal web local** (HTML/JS + JSON) via AP próprio, sem internet.  
- **RF02.** Medir **BPM** com **MAX30102** (compatível com MAX3010x).  
- **RF03.** Coletar **Energia**, **Humor** e **Ansiedade** (1–3) via **OLED + joystick**.  
- **RF04.** **Triagem** e **recomendação de cor** (verde/amarela/vermelha) a partir de BPM + escalas.  
- **RF05.** **Validação de cor** com **TCS34725** (normalização e limiares por **razões** entre canais).  
- **RF06.** **Consolidação em RAM**: média robusta de BPM, contagem por cor, médias das escalas.  
- **RF07.** Expor dados em tempo real no servidor HTTP local (**/** painel, **/display**, **/oled.json**, **/stats.json**, **/download.csv**).

### 3.2 Requisitos Não Funcionais (RNF)
- **RNF01.** Operar **100% offline**.  
- **RNF02.** **Baixo custo** e **montagem simples** (bom para demonstrações).  
- **RNF03.** **Interação rápida** por participante (meta ~30 s).  
- **RNF04.** **Privacidade:** processamento local e **CSV agregado** para exportação manual.  
- **RNF05.** **Usabilidade:** interface no OLED, feedback visual (cores/LEDs) e painel web direto.

---

## 4) Arquitetura (6 camadas IoT) e Modelagem

**Nó único** (Pico W/BitDogLab) operando como **Access Point** e **servidor HTTP** (lwIP). Sem internet externa.

1. **Sensor** — MAX30102 (BPM), TCS34725 (RGB/Clear), botões A/B e joystick.  
2. **Conectividade** — Wi-Fi AP do Pico W; serviços de **DHCP**, **DNS “cativo”** e **HTTP** na própria placa.  
3. **Borda/Processamento** — filtragem do PPG, detecção de picos e obtenção do **BPM final**; coleta das escalas; **`triage_decide`** (BPM + escalas → cor); classificação de cor por **razões** (R/G, etc.) após **normalização**.  
4. **Armazenamento** — estatísticas **em RAM** (média robusta de BPM, contagens por cor, médias das escalas; tamanho da amostra).  
5. **Abstração/Serviços** — endpoints HTTP: **/** (painel), **/display**, **/oled.json**, **/stats.json** (com `?color=`), **/download.csv**.  
6. **Exibição** — OLED local (4 linhas) + **painel web** responsivo acessível pelo AP.

**Considerações de privacidade e operação.** Todo processamento ocorre na borda (na placa), sem envio para a nuvem. O CSV é agregado (para registro/relato) e baixado manualmente.

---

## 5) Diagramas

### 5.1 Diagrama de Hardware

![Diagrama de Hardware](./etapa2/images/hardware_diagram.png)

**Explicação:**  
O diagrama organiza o sistema em **quatro blocos**: **Rede Wi-Fi**, **Placa BitDog (Pico W)**, **Sensores Externos (I²C0)** e **Alimentação**.  
- **Rede Wi-Fi:** a BitDog atua como **Access Point** e hospeda **AP/DHCP/DNS/HTTP**; o profissional conecta o celular/notebook diretamente ao AP e acessa as páginas via **HTTP** (opera **offline** com baixa latência).  
- **Placa BitDog:** o **RP2040** centraliza a lógica e conversa com os periféricos on-board: **OLED SSD1306 em I²C1 (SDA GP14, SCL GP15)**, **matriz WS2812 5×5 (DIN GP7)**, **LEDs RGB R/G/B em GP13/GP12/GP11**, **botões A/B em GP5/GP6**, **joystick SW em GP22 e eixos ADC VRx/VRy em GP27/GP26**, e **buzzers A/B em GP21/GP10**.  
- **Sensores Externos (I²C0):** o **I²C0 (SDA GP0, SCL GP1)** sai por um **extensor** até os módulos: **TCS34725 (0x29)** e **MAX30102 (0x57)**; ambos compartilham o barramento com **GND comum**.  
- **Alimentação:** **USB 5 V → 3V3** (estável) ou **bateria 18650 3,7 V → 3V3** (mobilidade). A lógica funciona em **3,3 V**; a **WS2812** recebe **5 V** com dados em 3,3 V (compatível na BitDog).

### 5.2 Diagrama de Blocos

![Diagrama de Blocos](./etapa2/images/blocos.png)

**Explicação:**  
1) **Gerenciador de Sensores** integra **MAX30102** (HR) e **TCS34725** (RGB/clear).  
2) **Validação e Processamento** verifica presença do dedo e qualidade do sinal, calcula **BPM final**, classifica a **cor**, e registra as escalas de **energia/humor/ansiedade** recebidas pelo **joystick**.  
3) **Agregador e Estatísticas** mantém **contagens/médias** em RAM e gera um **snapshot** único para exibição.  
4) **Feedback local**: telas no **OLED**, matriz/LEDs e buzzer.  
5) **Servidor HTTP** expõe **`/`**, **`/display`**, **`/oled.json`**, **`/stats.json`** e **`/download.csv`**.  
6) **Portal Web** (HTML/JS) acessado pelo AP exibe, **em tempo real**, os resultados processados — tudo **offline**.

### 5.3 Fluxograma de Software (versão atual)

> Use a imagem atualizada:

![Fluxograma de Software](./etapa2/images/fluxogram.png)

**Resumo do fluxo:**  
1) **Oxi prep** → mede **BPM final**  
2) **Escalas** no OLED (energia, humor, ansiedade)  
3) **Triagem** → cor recomendada (verde/amarelo/vermelho)  
4) **Validação de cor** com TCS34725 (normalização + razão)  
5) **Salvar** em stats (RAM) e **atualizar** OLED + painel web  
6) **Relatório rápido** no OLED → **repetir** ou **finalizar**

---

## 6) Endpoints HTTP e Painel Web

- **`GET /`** — **Painel** do profissional: gráfico curto de BPM, KPIs (médias de energia/humor/ansiedade), **barras por cor** e filtro por **cor/grupo**.  
- **`GET /display`** — espelho das **4 linhas** atuais do OLED (visual remoto).  
- **`GET /oled.json`** — `{"l1":"...","l2":"...","l3":"...","l4":"..."}`  
- **`GET /stats.json`** — resumo agregado; aceita `?color=verde|amarelo|vermelho`.  
  **Exemplo:**
  ```json
  {
    "bpm_live": 0.0,
    "bpm_mean": 78.2,
    "bpm_n": 12,
    "cores": {"verde":7,"amarelo":3,"vermelho":2},
    "ans_mean": 2.0,  "ans_n": 12,
    "energy_mean": 2.2, "energy_n": 12,
    "humor_mean": 2.4,  "humor_n": 12
  }

---

## 7) Imagens e Testes

![Protótipo do Projeto](./etapa3/fotos/image.png)

Na imagem acima é possível ver a BitDogLab conectada a um extensor i2c, que está fazendo a ponte entre os sensores de cor e o oxímetro com a BitDogLab. Também é possível ver o Display OLED disparando o texto para o usuário, porém é muito pequeno, por conta disso criamos um display externo que roda na mesma página do painel do profissional, para melhor visualização dos textos, com ele é possível que o usuário leia com mais facilidade os textos do display OLED:

![Tela do Display Externo](./etapa3/telas/oled_externo.png)


![Painel do Profissional](./etapa3/telas/painel.png)

Na imagem acima é possível ver o painel do profissional, que contêm um gráfico de barras contendo a quantidade de pacientes em cada grupo, o BPM médio do grupo, ansiedade, humor e energia média. Também é possível agrupar os dados por cada grupo, e fazer o download dos dados em uma planilha no formato CSV.

## Referências

1. UNICEF & ITU (2020) - “Two thirds of the world’s school-age children have no internet access at home.”
Link: https://www.unicef.org/press-releases/two-thirds-worlds-school-age-children-have-no-internet-access-home-new-unicef-itu

Link PDF: https://www.itu.int/en/ITU-D/Statistics/Documents/publications/UNICEF/How-many-children-and-young-people-have-internet-access-at-home-2020_v2final.pdf 

2. Yale Center for Emotional Intelligence — RULER (institucional)
Link: https://medicine.yale.edu/childstudy/services/community-and-schools-programs/center-for-emotional-intelligence/ruler/

3. RULER Approach — Mood Meter (recursos)
Link: https://rulerapproach.org/ruler-resources-for-families/

4. Shaffer, F.; Ginsberg, J. (2017) — “An Overview of Heart Rate Variability Metrics and Norms.” Frontiers in Public Health. 
Link: https://www.frontiersin.org/articles/10.3389/fpubh.2017.00258/full 

5. Kim, H. G., et al. (2018) — “Stress and Heart Rate Variability: A Meta-Analysis and Review of the Literature.” Psychiatry Investigation.
Link: https://pmc.ncbi.nlm.nih.gov/articles/PMC5900369/

6. FEMA — CERT Basic Training Participant Manual (2011) — “Simple Triage and Rapid Treatment (START)” (PDF)
Link: https://www.fema.gov/sites/default/files/2020-07/fema-cert_basic-training-participant-manual_01-01-2011.pdf

7. CERT-LA — START (folheto) (PDF)
Link: https://www.cert-la.com/downloads/education/english/start.pdf


