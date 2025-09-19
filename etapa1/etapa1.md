# TheraLink — Etapa 1 

## 1) Problema a ser resolvido

Em muitos contextos escolares e sociais sem internet confiável, falta um jeito simples e local de obter um retrato rápido do grupo; essa limitação é real — estimativas UNICEF/ITU indicam que cerca de dois terços das crianças e jovens não têm internet em casa, o que reforça a necessidade de soluções offline para atividades presenciais [1].

Inspiração do TheraLink. O desenho do TheraLink combina três ideias práticas: (i) a organização emocional em eixos simples (energia e valência), popularizada em ferramentas de SEL como RULER/Mood Meter do Yale Center for Emotional Intelligence, que transformam autorrelato em indicadores acionáveis [2][3]; (ii) o uso de um sinal fisiológico básico (BPM), alinhado a revisões que exploram variabilidade da frequência cardíaca (HRV) como indicador de estresse/arousal [4][5]; e (iii) uma visualização direta de prioridade por cores no estilo semáforo (verde, amarelo, vermelho), inspirada em esquemas de triagem como START, comuns em resposta a emergências [6][7]. Com isso, o profissional tem uma visão inicial do “clima” do grupo sem depender da nuvem.

---

## 2) Simulação do funcionamento na prática

1. Liga o dispositivo: a BitDogLab sobe um AP Wi-Fi local e inicializa periféricos.  
2. O participante posiciona o dedo no oxímetro MAX30102; o firmware coleta batimentos, filtra picos e obtém o BPM final.  
3. No OLED, com o joystick, informa Energia, Humor e Ansiedade (escala 1–3).  
4. O algoritmo de triagem combina BPM + escalas e recomenda uma cor (verde/amarela/vermelha).  
5. O participante aproxima a pulseira do sensor de cor TCS34725; o sistema valida a cor (normalização e razão entre canais) e confirma.  
6. As métricas são acumuladas em RAM (média robusta de BPM, contagem por cor, médias das escalas).  
7. A profissional acessa o painel local no celular/notebook conectado ao AP para acompanhar BPM, médias e contagem por cor em tempo real.  
8. O participante recebe feedback imediato no OLED (e LEDs); o painel também é atualizado.

---

## 3) Requisitos Funcionais (RF)

- RF01. Disponibilizar portal web local (HTML/JS + JSON) via AP próprio, sem internet.  
- RF02. Medir BPM no oxímetro MAX30102 (compatível com MAX3010x no firmware).  
- RF03. Coletar Energia, Humor e Ansiedade (1–3) via OLED + joystick.  
- RF04. Triagem e recomendação de cor (verde/amarela/vermelha) a partir de BPM + escalas.  
- RF05. Validação de cor com TCS34725 (normalização e limiares por razão entre canais).  
- RF06. Consolidação em RAM: média robusta de BPM, contagem por cor, médias das escalas.  
- RF07. Expor dados em tempo real no servidor HTTP local (/ para painel, /display, /oled.json, /stats.json, /download.csv).

---

## 4) Requisitos Não Funcionais (RNF)

- RNF01. Operar 100% offline.  
- RNF02. Baixo custo e montagem simples (adequado para demonstrações).  
- RNF03. Interação rápida por participante (meta ~30 s).  
- RNF04. Privacidade: processamento local e CSV agregado para exportação manual.  
- RNF05. Usabilidade: interface no OLED, feedback visual (cores/LEDs) e painel web direto.

---

## 5) Lista inicial de materiais

- 1× BitDogLab (RP2040/Pico W) com OLED, matriz 5×5, joystick e Wi-Fi  
- 1× MAX30102 (oxímetro I²C)  
- 1× TCS34725 (sensor de cor I²C)  
- Jumpers Dupont, fita dupla-face e fonte 5 V / power-bank  
- Opcional: pulseiras/tokens coloridos e etiqueta com instruções + QR code do painel

---

## Referências

[1] UNICEF & ITU (2020). “Two thirds of the world’s school-age children have no internet access at home.”  
Press release: https://www.unicef.org/press-releases/two-thirds-worlds-school-age-children-have-no-internet-access-home-new-report  
Relatório (PDF): https://www.itu.int/en/ITU-D/Statistics/Documents/publications/UNICEF/How-many-children-and-young-people-have-internet-access-at-home-2020_v2final.pdf

[2] Yale Center for Emotional Intelligence — RULER (institucional):  
https://medicine.yale.edu/childstudy/services/community-and-schools-programs/center-for-emotional-intelligence/ruler/

[3] RULER Approach — Mood Meter (recursos):  
https://rulerapproach.org/ruler-resources-for-families/

[4] Shaffer, F.; Ginsberg, J. (2017). “An Overview of Heart Rate Variability Metrics and Norms.” Frontiers in Public Health.  
https://www.frontiersin.org/articles/10.3389/fpubh.2017.00258/full

[5] Kim, H. G., et al. (2018). “Stress and Heart Rate Variability: A Meta-Analysis and Review of the Literature.” Psychiatry Investigation.  
https://pmc.ncbi.nlm.nih.gov/articles/PMC5900369/

[6] FEMA — CERT Basic Training Participant Manual (2011) — “Simple Triage and Rapid Treatment (START)” (PDF):  
https://www.fema.gov/sites/default/files/2020-07/fema-cert_basic-training-participant-manual_01-01-2011.pdf

[7] CERT-LA — “START: Simple Triage and Rapid Treatment” (folheto) (PDF):  
https://www.cert-la.com/downloads/education/english/start.pdf
